// BASS, LEAD, PAD: one synth engine, role-specific model lists and defaults.
// Voices render oversampled from band-limited mipmapped tables (saw, square, triangle, PWM as the
// difference of two saws, BLEP-corrected hard sync): 2x for the linear-filter models, 4x for FM, the
// models with a driven ladder / MS20 filter and for hard sync. DRIVE and the MS20 saturation use
// antiderivative anti-aliasing on top.
// Halfband decimators bring the sums back down: no audible aliasing at any pitch.
#include "Kit.h"
#include "../core/BandLimited.h"
#include "../core/Oversample.h"
#include "Calibration.h"

namespace sf
{
// (SF-000) GRANULAR's read of an octave-down copy of its source, at a sample a step or less: sixteen points from a table (a
// Kaiser-windowed sinc cut at the copy's own Nyquist, 256 fractions of a sample, the two nearest rows blended), both
// channels in the one pass. A grain above the source's pitch used to read the copy an octave above this one through
// sampleAt's low-pass, 18 to 36 points a channel with a table look-up for each: sixteen notes of four grains at twice the
// rate spent a core and a half there. Measured on tones (a copy read at 0.51 to 1 sample a step, then halved as the voice's
// sum is): level within 0.01 dB to 18 kHz and 0.3 dB at 20 kHz, everything else under 20 kHz more than 78 dB below the tone.
struct GrainRead
{
    static constexpr int kTaps = 16, kPhases = 256;
    static const GrainRead& get() { static const GrainRead g; return g; }
    // the two channels' sum at a fractional position (near an end, where the table would reach past it: four points, as before)
    float sum (const float* a, const float* b, int len, double pos) const noexcept
    {
        const int i = (int) pos, first = i - (kTaps / 2 - 1);
        if (first < 0 || first + kTaps > len) return hermiteAt (a, len, pos) + hermiteAt (b, len, pos);
        const float ph = (float) (pos - (double) i) * (float) kPhases;
        const int p = std::min (kPhases - 1, (int) ph);
        const float t = ph - (float) p;
        const float* r0 = w[p]; const float* r1 = w[p + 1];
        const float* xa = a + first; const float* xb = b + first;
        // (four sums side by side, so that the compiler may do four points at a time: one sum in order cannot be split)
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        for (int k = 0; k < kTaps; k += 4)
        {
            s0 += (r0[k] + t * (r1[k] - r0[k])) * (xa[k] + xb[k]);
            s1 += (r0[k + 1] + t * (r1[k + 1] - r0[k + 1])) * (xa[k + 1] + xb[k + 1]);
            s2 += (r0[k + 2] + t * (r1[k + 2] - r0[k + 2])) * (xa[k + 2] + xb[k + 2]);
            s3 += (r0[k + 3] + t * (r1[k + 3] - r0[k + 3])) * (xa[k + 3] + xb[k + 3]);
        }
        return (s0 + s1) + (s2 + s3);
    }
    // (SF-000) the two channels apart, at the same place: a grain keeps the recording's own left and right. (A file of one
    // channel gives the two the same, and each is half of what sum gives, to the bit.)
    void both (const float* a, const float* b, int len, double pos, float& l, float& r) const noexcept
    {
        const int i = (int) pos, first = i - (kTaps / 2 - 1);
        if (first < 0 || first + kTaps > len) { l = hermiteAt (a, len, pos); r = a == b ? l : hermiteAt (b, len, pos); return; }
        const float ph = (float) (pos - (double) i) * (float) kPhases;
        const int p = std::min (kPhases - 1, (int) ph);
        const float t = ph - (float) p;
        const float* r0 = w[p]; const float* r1 = w[p + 1];
        const float* xa = a + first; const float* xb = b + first;
        float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
        for (int k = 0; k < kTaps; k += 4)
        {
            const float w0 = r0[k] + t * (r1[k] - r0[k]), w1 = r0[k + 1] + t * (r1[k + 1] - r0[k + 1]);
            const float w2 = r0[k + 2] + t * (r1[k + 2] - r0[k + 2]), w3 = r0[k + 3] + t * (r1[k + 3] - r0[k + 3]);
            a0 += w0 * xa[k]; a1 += w1 * xa[k + 1]; a2 += w2 * xa[k + 2]; a3 += w3 * xa[k + 3];
            b0 += w0 * xb[k]; b1 += w1 * xb[k + 1]; b2 += w2 * xb[k + 2]; b3 += w3 * xb[k + 3];
        }
        l = (a0 + a1) + (a2 + a3); r = (b0 + b1) + (b2 + b3);
    }
private:
    GrainRead()
    {
        const double beta = 7.0, pi = 3.14159265358979323846;
        auto i0 = [] (double x) { double sum = 1.0, term = 1.0; for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; } return sum; };
        for (int p = 0; p <= kPhases; ++p)
        {
            double h[kTaps], all = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const double t = (double) (k - (kTaps / 2 - 1)) - (double) p / kPhases, u = t / (kTaps / 2.0);
                const double win = std::abs (u) < 1.0 ? i0 (beta * std::sqrt (1.0 - u * u)) / i0 (beta) : 0.0;
                h[k] = (std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t)) * win;
                all += h[k];
            }
            for (int k = 0; k < kTaps; ++k) w[p][k] = (float) (h[k] / all);       // (each fraction's points add up to one: no ripple at DC)
        }
    }
    float w[kPhases + 1][kTaps];
};

enum Engine { E_SUB, E_ORGAN, E_FM, E_HOOVER, E_SYNC, E_NOISE, E_WAVETABLE, E_GRAIN, E_SAMPLE, E_FM4 };

// FM 4-OP [Nov 10]: four sine operators, A B C D, on one of eight algorithms (D's output can feed
// itself back). Modulation only ever runs down the letters (D into C, B or A; C into B or A; B into
// A), so the operators are worked out D, C, B, A. mods[k]: the operators that modulate k (a bit
// each); carriers: the ones heard.
struct FmAlgo { uint8_t mods[4]; uint8_t carriers; };
static const FmAlgo kFmAlgos[8] = {
    { { 1 << 1, 1 << 2, 1 << 3, 0 }, 1 },                          // STACK      D > C > B > A
    { { 1 << 1, (1 << 2) | (1 << 3), 0, 0 }, 1 },                  // Y          C and D > B > A
    { { (1 << 1) | (1 << 2), 0, 1 << 3, 0 }, 1 },                  // BRANCH     D > C, then C and B > A
    { { 1 << 1, 0, 1 << 3, 0 }, (1 << 0) | (1 << 2) },             // 2 STACKS   B > A, D > C
    { { 1 << 3, 1 << 3, 1 << 3, 0 }, 7 },                          // 1 TO 3     D > A, B and C
    { { 0, 1 << 2, 1 << 3, 0 }, 3 },                               // STACK + 1  D > C > B, and A
    { { 1 << 2, 1 << 2, 1 << 3, 0 }, 3 },                          // BELL       D > C > A and B
    { { 0, 0, 0, 0 }, 15 } };                                      // ORGAN      all four heard
static std::vector<std::string> fmAlgoNames() { return { "STACK", "Y", "BRANCH", "2 STACKS", "1 TO 3", "STACK + 1", "BELL", "ORGAN" }; }
static const float kFmRatios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 14.0f, 16.0f };
static std::vector<std::string> fmRatioNames() { return { "0.5", "1", "1.5", "2", "2.5", "3", "3.5", "4", "5", "6", "7", "8", "9", "10", "11", "12", "14", "16" }; }
enum FiltType { F_LADDER, F_SVF, F_MS20, F_NONE, F_DIODE };   // F_DIODE: the TB-303's (ACID 303-TYPE)

struct SynthModel
{
    const char* name;
    Engine engine;
    int wave = 0, wave2 = 0;        // 0 saw 1 square 2 tri 3 sine
    float osc2 = 0.0f, detune = 0.0f, sub = 0.0f, noise = 0.0f;
    int unison = 1;
    FiltType filt = F_LADDER;
    float resMul = 1.0f, envMul = 1.0f;
    float att = 0.002f, dec = 0.3f, sus = 0.6f, rel = 0.1f;       // amp
    float fAtt = 0.001f, fDec = 0.3f, fSus = 0.0f, fRel = 0.2f;   // filter
    bool mono = false, acid = false;
    bool tilt = false;      // (SF-000) a model that reads no COLOR of its own: COLOR tilts it darker or brighter about the centre
    int chord = 0;                  // 0 none, else index into chord table
    float ensemble = 0.0f, drive = 0.0f, wow = 0.0f, ratio = 1.0f, pitchDive = 0.0f;
    float gain = 1.0f;
};

// Chords (semitones). Index 0 = single note.
static const std::vector<std::vector<int>>& chordTable()
{
    static const std::vector<std::vector<int>> t { { 0 }, { 0, 3, 7, 10 }, { 0, 3, 10, 14 }, { 0, 5, 7, 12 }, { 0, 4, 7, 11 }, { 0, 3, 7 }, { 0, 7 }, { 0, 12 } };
    return t;
}
static std::vector<std::string> chordNames() { return { "MODEL", "NONE", "MIN7", "MIN9", "SUS4", "MAJ7", "MIN", "5TH", "OCT" }; }

static std::vector<SynthModel> bassModels()
{
    std::vector<SynthModel> m;
    { SynthModel x { "BASS LADDER", E_SUB }; x.gain = 1.6f; x.wave = 0; x.wave2 = 0; x.osc2 = 0.8f; x.detune = 0.08f; x.sub = 0.4f; x.mono = true; x.dec = 0.5f; x.sus = 0.8f; x.fDec = 0.35f; m.push_back (x); }
    { SynthModel x { "MONO 101-TYPE", E_SUB }; x.wave = 0; x.wave2 = 1; x.osc2 = 0.5f; x.sub = 0.5f; x.mono = true; x.dec = 0.4f; x.sus = 0.7f; x.fDec = 0.25f; m.push_back (x); }
    { SynthModel x { "SUB OSC", E_SUB }; x.wave = 1; x.sub = 0.9f; x.filt = F_SVF; x.mono = true; x.dec = 0.6f; x.sus = 0.9f; x.envMul = 0.4f; m.push_back (x); }
    { SynthModel x { "ACID 303-TYPE", E_SUB }; x.wave = 0; x.mono = true; x.acid = true; x.filt = F_DIODE; x.att = 0.003f; x.dec = 3.0f; x.sus = 0.0f; x.rel = 0.008f; m.push_back (x); }
    { SynthModel x { "FM", E_FM }; x.ratio = 1.0f; x.mono = true; x.filt = F_SVF; x.dec = 0.5f; x.sus = 0.7f; x.fDec = 0.2f; m.push_back (x); }
    { SynthModel x { "SINE SUB", E_SUB }; x.wave = 3; x.filt = F_SVF /* (SF-000) was none: CUTOFF, RES, ENV AMT */; x.mono = true; x.dec = 0.5f; x.sus = 0.7f /* (SF-000) was 1 s to all of it: DECAY */; x.envMul = 0.3f; x.drive = 0.15f; m.push_back (x); }
    { SynthModel x { "OFFBEAT", E_SUB }; x.wave = 1; x.wave2 = 0; x.osc2 = 0.6f; x.detune = 0.05f; x.mono = true; x.dec = 0.18f; x.sus = 0.0f; x.fDec = 0.12f; x.resMul = 1.3f; x.envMul = 1.3f; m.push_back (x); }
    { SynthModel x { "SAMPLE", E_SAMPLE }; x.filt = F_SVF; x.mono = true; x.dec = 1.0f; x.sus = 1.0f; x.envMul = 0.0f; m.push_back (x); }
    // (SF-000) WAVETABLE, as PAD has it (TABLE: the built-in waves, or eight from the loaded sample; COLOR across them).
    // Second to last, so the first and the last of the list are who they were: a place kept from before is told by that.
    { SynthModel x { "WAVETABLE", E_WAVETABLE }; x.gain = 1.4f; x.filt = F_SVF; x.mono = true; x.att = 0.002f; x.dec = 0.6f; x.sus = 0.8f; x.rel = 0.08f; x.envMul = 0.5f; m.push_back (x); }
    { SynthModel x { "FM 4-OP", E_FM4 }; x.filt = F_SVF; x.mono = true; x.att = 0.002f; x.dec = 0.8f; x.sus = 0.9f; x.rel = 0.08f; x.fDec = 0.3f; x.envMul = 0.3f; m.push_back (x); }
    // (SF-000) GRANULAR, as PAD has it, with a bass's envelope and one voice. Last on the list: every place before it is who it was.
    { SynthModel x { "GRANULAR", E_GRAIN }; x.gain = 2.1f /* (SF-000) level with BASS's other models: --audit 6 --model ALL --level */; x.filt = F_SVF; x.mono = true; x.att = 0.004f; x.dec = 1.0f; x.sus = 0.8f; x.rel = 0.12f; x.envMul = 0.0f; m.push_back (x); }
    return m;
}
static std::vector<SynthModel> leadModels()
{
    std::vector<SynthModel> m;
    { SynthModel x { "ACID 303-TYPE", E_SUB }; x.wave = 0; x.mono = true; x.acid = true; x.filt = F_DIODE; x.att = 0.003f; x.dec = 3.0f; x.sus = 0.0f; x.rel = 0.008f; x.ensemble = 0.2f /* (SF-000) SPACE */; m.push_back (x); }
    { SynthModel x { "MONO 101-TYPE", E_SUB }; x.wave = 0; x.wave2 = 1; x.osc2 = 0.5f; x.sub = 0.3f; x.mono = true; x.dec = 0.4f; x.sus = 0.6f; x.fDec = 0.25f; x.ensemble = 0.2f /* (SF-000) SPACE */; m.push_back (x); }
    { SynthModel x { "MS20-TYPE", E_SUB }; x.wave = 0; x.wave2 = 1; x.osc2 = 0.6f; x.detune = 0.1f; x.filt = F_MS20; x.mono = true; x.drive = 0.4f; x.dec = 0.5f; x.sus = 0.7f; x.resMul = 1.2f; x.ensemble = 0.2f /* (SF-000) SPACE */; m.push_back (x); }
    { SynthModel x { "STAB DUB", E_SUB }; x.wave = 0; x.wave2 = 1; x.osc2 = 0.5f; x.detune = 0.06f; x.filt = F_SVF; x.chord = 1; x.dec = 0.25f; x.sus = 0.0f; x.rel = 0.15f; x.fDec = 0.12f; x.envMul = 0.8f; x.ensemble = 0.3f; m.push_back (x); }
    { SynthModel x { "STAB DETROIT", E_SUB }; x.gain = 2.0f; x.wave = 0; x.unison = 3; x.detune = 0.15f; x.wave2 = 1; x.osc2 = 0.3f; x.filt = F_SVF; x.chord = 2; x.dec = 0.35f; x.sus = 0.2f; x.rel = 0.25f; x.fDec = 0.2f; x.ensemble = 0.6f; m.push_back (x); }
    { SynthModel x { "ORGAN M1-TYPE", E_ORGAN }; x.filt = F_SVF /* (SF-000) was none */; x.chord = 5; x.dec = 0.3f; x.sus = 0.3f; x.rel = 0.08f; x.ensemble = 0.2f; m.push_back (x); }
    { SynthModel x { "HOOVER", E_HOOVER }; x.unison = 3; x.detune = 0.25f; x.filt = F_SVF; x.dec = 1.0f; x.sus = 0.9f; x.rel = 0.3f; x.ensemble = 0.8f; x.pitchDive = 7.0f; x.mono = true; x.tilt = true /* (SF-000) COLOR */; m.push_back (x); }
    { SynthModel x { "FM BELL", E_FM }; x.ratio = 3.5f; x.filt = F_SVF /* (SF-000) was none */; x.dec = 1.5f; x.sus = 0.0f; x.rel = 1.2f; x.fDec = 0.6f; x.ensemble = 0.2f /* (SF-000) SPACE */; m.push_back (x); }
    { SynthModel x { "SYNC", E_SYNC }; x.mono = true; x.dec = 0.5f; x.sus = 0.7f; x.fDec = 0.4f; x.filt = F_SVF; x.ensemble = 0.2f /* (SF-000) SPACE */; m.push_back (x); }
    { SynthModel x { "SAMPLE", E_SAMPLE }; x.filt = F_SVF; x.dec = 1.0f; x.sus = 1.0f; x.envMul = 0.0f; m.push_back (x); }
    // (SF-000) WAVETABLE (see BASS), second to last.
    { SynthModel x { "WAVETABLE", E_WAVETABLE }; x.gain = 1.4f; x.filt = F_SVF; x.att = 0.003f; x.dec = 0.5f; x.sus = 0.6f; x.rel = 0.2f; x.ensemble = 0.3f; x.detune = 0.12f; m.push_back (x); }
    { SynthModel x { "FM 4-OP", E_FM4 }; x.filt = F_SVF /* (SF-000) was none */; x.att = 0.002f; x.dec = 1.0f; x.sus = 0.9f; x.rel = 0.3f; x.ensemble = 0.3f /* (SF-000) SPACE */; m.push_back (x); }
    // (SF-000) GRANULAR (see BASS), last on the list.
    { SynthModel x { "GRANULAR", E_GRAIN }; x.gain = 3.5f /* (SF-000) level with LEAD's and CHORD's other models: --audit 8 --model ALL --level */; x.filt = F_SVF; x.att = 0.01f; x.dec = 1.0f; x.sus = 0.8f; x.rel = 0.4f; x.envMul = 0.0f; x.ensemble = 0.3f; m.push_back (x); }
    return m;
}
static std::vector<SynthModel> padModels()
{
    std::vector<SynthModel> m;
    { SynthModel x { "STRING MACHINE", E_SUB }; x.gain = 2.0f; x.wave = 0; x.unison = 4; x.detune = 0.2f; x.wave2 = 0; x.osc2 = 0.5f; x.ratio = 2.0f; x.filt = F_SVF; x.att = 0.3f; x.dec = 1.0f; x.sus = 0.75f /* (SF-000) was 1: DECAY */; x.rel = 0.8f; x.envMul = 0.1f; x.ensemble = 1.0f; x.tilt = true /* (SF-000) COLOR */; m.push_back (x); }
    { SynthModel x { "POLY PAD", E_SUB }; x.gain = 1.6f; x.wave = 0; x.wave2 = 1; x.osc2 = 0.6f; x.detune = 0.1f; x.sub = 0.3f; x.filt = F_SVF; x.att = 0.5f; x.dec = 1.0f; x.sus = 1.0f; x.rel = 1.2f; x.fAtt = 0.8f; x.fDec = 2.0f; x.fSus = 0.4f; x.envMul = 0.4f; x.ensemble = 0.7f; m.push_back (x); }
    { SynthModel x { "WAVETABLE", E_WAVETABLE }; x.gain = 1.4f; x.filt = F_SVF; x.att = 0.4f; x.dec = 1.0f; x.sus = 0.7f /* (SF-000) was 1: DECAY is heard */; x.rel = 1.0f; x.envMul = 0.2f; x.ensemble = 0.3f; x.detune = 0.12f /* (SF-000) DETUNE acts on it */; m.push_back (x); }
    { SynthModel x { "GRANULAR", E_GRAIN }; x.gain = 1.5f; x.filt = F_SVF; x.att = 0.6f; x.dec = 1.0f; x.sus = 0.7f /* (SF-000) was 1: DECAY */; x.rel = 1.5f; x.envMul = 0.0f; x.ensemble = 0.3f /* (SF-000) SPACE */; m.push_back (x); }
    { SynthModel x { "DUB WASH", E_SUB }; x.wave = 0; x.wave2 = 1; x.osc2 = 0.4f; x.detune = 0.05f; x.filt = F_SVF; x.chord = 1; x.att = 0.05f; x.dec = 1.5f; x.sus = 0.6f; x.rel = 1.5f; x.fDec = 1.5f; x.envMul = 0.5f; x.ensemble = 0.5f; m.push_back (x); }
    { SynthModel x { "TAPE LOOP", E_SUB }; x.wave = 2; x.wave2 = 0; x.osc2 = 0.3f; x.detune = 0.03f; x.filt = F_SVF; x.att = 0.4f; x.dec = 1.0f; x.sus = 0.7f /* (SF-000) was 1: DECAY */; x.rel = 1.5f; x.envMul = 0.0f; x.wow = 1.0f; x.drive = 0.3f; x.noise = 0.02f; x.ensemble = 0.3f; x.tilt = true /* (SF-000) SPACE, COLOR */; m.push_back (x); }
    { SynthModel x { "NOISE BED", E_NOISE }; x.filt = F_SVF; x.att = 0.8f; x.dec = 1.0f; x.sus = 0.7f /* (SF-000) was 1: DECAY */; x.rel = 2.0f; x.envMul = 0.0f; x.ensemble = 0.3f /* (SF-000) SPACE */; m.push_back (x); }
    { SynthModel x { "SAMPLE", E_SAMPLE }; x.filt = F_SVF; x.att = 0.05f; x.dec = 1.0f; x.sus = 1.0f; x.rel = 0.8f; x.envMul = 0.0f; m.push_back (x); }
    { SynthModel x { "FM 4-OP", E_FM4 }; x.filt = F_SVF /* (SF-000) was none */; x.att = 0.25f; x.dec = 1.0f; x.sus = 1.0f; x.rel = 1.2f; x.ensemble = 0.3f; m.push_back (x); }
    return m;
}

// Hard-sync saw (master resets slave), polyBLEP at every edge. One sample of delay so the
// sample before an edge can be corrected too.
struct SyncSaw
{
    double pm = 0.0, ps = 0.0;
    float held = 0.0f;
    float tick (double im, double is)
    {
        pm += im; ps += is;
        float now = 0.0f, before = 0.0f;
        auto step = [&] (double jump, double d)            // d: samples since the edge (0..1)
        {
            now += (float) (0.5 * jump * (2.0 * d - d * d - 1.0));
            before += (float) (0.5 * jump * d * d);
        };
        if (pm >= 1.0)
        {
            pm -= 1.0;
            const double d = pm / im;
            double pr = ps - d * is;                        // slave phase at the reset
            if (pr >= 1.0) { step (-2.0, (ps - 1.0) / is); pr -= 1.0; }
            step (-2.0 * pr, d);
            ps = d * is;
        }
        else if (ps >= 1.0)
        {
            ps -= 1.0;
            step (-2.0, ps / is);
        }
        const float out = held + before;
        held = (float) (2.0 * ps - 1.0) + now;
        return out;
    }
};

// Exponential ADSR.
struct Adsr
{
    enum { Idle, Att, Dec, Sus, Rel } st = Idle;
    float v = 0.0f, a = 0, d = 0, s = 0, r = 0;
    void set (float at, float dt, float sl, float rt, double sr)
    {
        a = 1.0f / (float) (std::max (0.0005f, at) * sr);
        d = std::exp (-1.0f / (float) (std::max (0.001f, dt) * sr * 0.3f));
        s = sl;
        r = std::exp (-1.0f / (float) (std::max (0.001f, rt) * sr * 0.3f));
    }
    void on (bool retrigger = true) { if (retrigger || st == Idle || st == Rel) st = Att; }
    void off() { if (st != Idle) st = Rel; }
    float tick()
    {
        switch (st)
        {
            case Att: v += a; if (v >= 1.0f) { v = 1.0f; st = Dec; } break;
            case Dec: v = s + d * (v - s); if (std::abs (v - s) < 1e-4f) st = Sus; break;
            case Sus: v = s; break;
            case Rel: v *= r; if (v < 1e-5f) { v = 0; st = Idle; } break;
            case Idle: break;
        }
        return v;
    }
};

// Stereo ensemble (3 modulated delays, BBD-like).
struct Ensemble
{
    DelayLine d[2];
    float ph = 0.0f;
    double sr = 48000.0;
    void prepare (double rate) { sr = rate; for (auto& x : d) x.prepare ((int) (rate * 0.05)); }
    void reset() { for (auto& x : d) x.clear(); }
    void process (float* L, float* R, int n, float amt)
    {
        if (amt <= 0.001f) { for (int i = 0; i < n; ++i) { d[0].push (L[i]); d[1].push (R[i]); } return; }
        for (int i = 0; i < n; ++i)
        {
            ph += 0.6f / (float) sr; if (ph > 1.0f) ph -= 1.0f;
            const float m = 0.5f * (L[i] + R[i]);
            d[0].push (m); d[1].push (m);
            float wl = 0, wr = 0;
            for (int k = 0; k < 3; ++k)
            {
                const float lfo = std::sin (kTwoPi * (ph + k / 3.0f)) + 0.15f * std::sin (kTwoPi * (ph * 9.0f + k / 3.0f));
                const float ms = (7.0f + 4.0f * lfo) * 0.001f * (float) sr;
                const float y = d[0].read (ms);
                if (k == 0) wl += y; else if (k == 1) wr += y; else { wl += 0.5f * y; wr += 0.5f * y; }
            }
            L[i] = L[i] * (1.0f - amt * 0.5f) + wl * amt * 0.7f;
            R[i] = R[i] * (1.0f - amt * 0.5f) + wr * amt * 0.7f;
        }
    }
};

class SynthModule : public Module
{
public:
    // SSTART: the sample's START; END, FADE IN, FADE OUT follow (sampleRegionParams).
    // ACID and WARP, then SPLIT, SPLIT HZ, DRIFT and TABLE [Nov 10], after the sample's four (ids
    // are appended, never moved).
    enum { CUTOFF, RES, ENVAMT, GATE, DENS, FILT, DRIVE, LEVEL, MODEL, DECAY, ATTACK, ACCENT, GLIDE, TUNE, DETUNE, COLOR,
           LAYERMODEL, LAYER, CHORD, SPACE, PDEPTH, PSHAPE, ESPACE, ESWING, ALLOCON, KEYFOLLOW, SSTART, ACID = SSTART + 4, WARP,
           SPLIT, SPLITHZ, DRIFT, TABLE,
           FMALGO, FMRATIO, FMLEVEL = FMRATIO + 4, FMDECAY = FMLEVEL + 4, FMFB = FMDECAY + 4,
           SGAIN, SNORM, SPLAY, SENV,       // (SF-000) the sample's own: its gain, NORMALISE, PLAY (held or through), ENVELOPE
           // (SF-000) GRANULAR's own (see E_GRAIN)
           GRPOS, GRSCAN, GRSIZE, GRDENS, GRSPRAY, GRPITCH, GRSHAPE, GRREV, GRCLOCK, GRSPREAD, GRFOLLOW, GRSYNC,
           // (SF-000) SAMPLE's: what a note chooses, the loop, and where a note starts
           SNOTES, SLOOP, SXFADE, SVELSTART, SRANDSTART,
           SCUTS, SCUTLAST = SCUTS + kCuts,      // (SF-000) MARKERS and the 32 slice markers
           kParams };

    SynthModule (const char* idName, int role, std::vector<SynthModel> m, float cutDef, float resDef, float envDef, float gateDef, float pDepth)
        : name (idName), roleId (role), models (std::move (m)), dCut (cutDef), dRes (resDef), dEnv (envDef), dGate (gateDef), dPocket (pDepth) {}

    std::string id() const override { return name; }
    bool instrument() const override { return true; }
    int latency() const override { return Pocket::kLatency; }
    double tailSeconds() const override { return 6.0; }
    std::vector<ParamSpec> params() const override
    {
        std::vector<std::string> mn; for (auto& m : models) mn.push_back (m.name);
        auto p = withRegion ({ pct ("cutoff", "CUTOFF", dCut).prim().dir (1),
                 pct ("res", "RES", dRes).prim(),
                 pct ("env_amt", "ENV AMT", dEnv).prim().dir (1),
                 pct ("gate", "GATE", dGate).prim(),
                 pct ("density", "DENSITY", 0.0f).prim().dir (1),
                 bip ("filter", "FILTER").prim(),
                 pct ("drive", "DRIVE", 0.0f).prim().dir (1),
                 levelParam (-6.0f).prim(),
                 choice ("model", "MODEL", mn, 0),
                 pct ("decay", "DECAY", 0.5f).slow(),
                 pct ("attack", "ATTACK", 0.0f).slow(),
                 pct ("accent", "ACCENT", 0.5f).slow(),
                 pct ("glide", "GLIDE", 0.2f).slow(),
                 knob ("tune", "TUNE", -24.0f, 24.0f, 0.0f, "st"),
                 pct ("detune", "DETUNE", 0.5f).slow(),
                 pct ("color", "COLOR", 0.5f).slow(),
                 choice ("layer_model", "LAYER MODEL", mn, std::min<int> (1, (int) mn.size() - 1)).layerGroup(),
                 pct ("layer", "LAYER", 0.0f).layerGroup(),
                 choice ("chord", "CHORD", chordNames(), 0),
                 pct ("space", "SPACE", 0.5f).slow(),
                 pct ("pocket_depth", "POCKET DEPTH", dPocket).slow(),
                 pct ("pocket_shape", "POCKET SHAPE", 0.25f),
                 choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 1),
                 pct ("echo_swing", "ECHO SWING", 0.0f),
                 toggle ("alloc", "ALLOC", true).noPreset().forget(),
                 toggle ("key_follow", "KEY FOLLOW", true).noPreset().forget() });
        // The E16's NRG page [Nov 10]: ACID on the 303's track, WARP on the PAD's (any model takes them).
        p.push_back (pct ("acid", "ACID", 0.0f).slow().dir (1));
        p.push_back (pct ("warp", "WARP", 0.0f).slow().dir (1));
        // LAYER SPLIT (Kit.h): which layer plays the lows, which the mids and highs.
        p.push_back (knob ("split", "SPLIT", 0.0f, 1.0f, 0.5f, "SPLIT").layerGroup());
        p.push_back (knob ("split_hz", "SPLIT HZ", 50.0f, 800.0f, 150.0f, "Hz", 200.0f).layerGroup());
        // Analog drift (updateControls).
        p.push_back (pct ("drift", "DRIFT", 0.0f).slow());
        // WAVETABLE's frames: the built-in four, or eight from the loaded sample (hidden where no
        // model is WAVETABLE).
        auto table = choice ("table", "TABLE", { "BUILT-IN", "SAMPLE" }, 0).engineGroup();
        if (! hasWavetable()) table.hide();
        p.push_back (table);
        // FM 4-OP's operators [Nov 10]: the algorithm, each operator's RATIO (to the note), LEVEL
        // (a carrier's loudness, a modulator's depth) and DECAY (its own envelope), D's FEEDBACK.
        p.push_back (choice ("fm_algo", "ALGORITHM", fmAlgoNames(), 3).engineGroup());
        const char* const ops[] = { "A", "B", "C", "D" };
        const int ratioDef[] = { 1, 1, 3, 10 };                    // 1, 1, 2, 7
        const float levelDef[] = { 1.0f, 0.5f, 0.6f, 0.25f }, decayDef[] = { 0.7f, 0.4f, 0.6f, 0.25f };
        std::string ids[12], names[12];
        for (int k = 0; k < 4; ++k)
        {
            ids[k] = std::string ("fm_") + (char) ('a' + k) + "_ratio";  names[k] = std::string (ops[k]) + " RATIO";
            ids[4 + k] = std::string ("fm_") + (char) ('a' + k) + "_level"; names[4 + k] = std::string (ops[k]) + " LEVEL";
            ids[8 + k] = std::string ("fm_") + (char) ('a' + k) + "_decay"; names[8 + k] = std::string (ops[k]) + " DECAY";
        }
        for (int k = 0; k < 4; ++k) { auto q = choice ("x", "x", fmRatioNames(), ratioDef[k]).engineGroup(); q.id = ids[k]; q.name = names[k]; p.push_back (q); }
        for (int k = 0; k < 4; ++k) { auto q = pct ("x", "x", levelDef[k]).engineGroup(); q.id = ids[4 + k]; q.name = names[4 + k]; p.push_back (q); }
        for (int k = 0; k < 4; ++k) { auto q = pct ("x", "x", decayDef[k]).engineGroup(); q.id = ids[8 + k]; q.name = names[8 + k]; p.push_back (q); }
        p.push_back (pct ("fm_feedback", "FEEDBACK", 0.2f).engineGroup());
        // (SF-000) A sample's own controls, with START and END. GAIN and NORMALISE (Kit.h). PLAY: HELD, it sounds while
        // its note is held; THROUGH, a note plays it to its END however short the note (a one-shot). ENVELOPE: the
        // lane's DECAY and ENV AMT act on it as on a synth model (DECAY at full: no decay); off, they leave it alone.
        p = withSampleGain (std::move (p));
        p.push_back (choice ("smp_play", "PLAY", { "HELD", "THROUGH" }, 0).noPreset().sampleGroup());
        // KNOBS (on as a sample opens): every knob of the lane acts on a sample. DECAY is its decay (the top fifth of the
        // knob brings the sustain up to all of it), ENV AMT sweeps the filter, COLOR tilts it darker or brighter about
        // the centre, DETUNE adds a second reading a few cents off and SPACE the ensemble (not on a mono model), and in
        // PLAY THROUGH GATE is how much of the sample plays. Off: as samples played before, all of those asleep.
        p.push_back (toggle ("smp_env", "KNOBS", true).noPreset().sampleGroup());
        // (SF-000) GRANULAR's own knobs. Until now the grains had two knobs shared with other things (COLOR their size,
        // DETUNE their scatter and pitch spread), four to a grain's length always, and the place they came from drifted
        // through the source at one fixed pace. Where each of these starts is what GRANULAR did before, so nothing that
        // played it changes until one is turned.
        //   POSITION  where in the source (its START..END) the grains come from
        //   SCAN      how fast that place travels as a note is held, as a pace of the source's own: stopped in the middle
        //             (the place stands still: a held note is a frozen moment), forwards to the right, backwards to the left
        //   SIZE      a grain's length, 5 ms to half a second
        //   GRAINS    how many overlap: one in two lengths (gaps between them) up to eight at once. On a CLOCK: how likely
        //             each tick is to fire
        //   SPRAY     how far from the place each grain may start: none, to anywhere in the source
        //   PITCH     how far each grain's pitch may stray: FINE a few cents, or by OCTAVES, or FIFTHS AND OCTAVES (SPREAD)
        //   SHAPE     a grain's envelope: struck (a click and a decay) at the left, rounded in the middle, held flat at the right
        //   REVERSE   how many of the grains play backwards
        //   CLOCK     FREE: grains follow each other at their own length. Or on the song's grid: a grain a tick
        //   FOLLOW    NOTES: a grain is pitched by the note. FIXED: at the source's own pitch whatever the note (TUNE still shifts)
        //   SYNC      OFF. Or the source's START..END spans that many beats of the song, and the place is where the song
        //             is in them: a recorded loop holds the tempo whatever the note, and SCAN is not used
        p.push_back (pct ("gr_pos", "POSITION", 0.0f).slow().engineGroup());
        p.push_back (knob ("gr_scan", "SCAN", -1.0f, 1.0f, 0.1581139f, "BIP").slow().engineGroup());
        p.push_back (pct ("gr_size", "SIZE", 0.7456836f).slow().engineGroup());
        p.push_back (pct ("gr_dens", "GRAINS", 0.75f).slow().engineGroup());
        p.push_back (pct ("gr_spray", "SPRAY", 0.3872983f).slow().engineGroup());
        p.push_back (pct ("gr_pitch", "PITCH", 0.25f).slow().engineGroup());
        p.push_back (pct ("gr_shape", "SHAPE", 0.5f).slow().engineGroup());
        p.push_back (pct ("gr_rev", "REVERSE", 0.0f).slow().engineGroup());
        p.push_back (choice ("gr_clock", "CLOCK", { "FREE", "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }, 0).engineGroup());
        p.push_back (choice ("gr_spread", "SPREAD", { "FINE", "OCTAVES", "FIFTHS AND OCTAVES" }, 0).engineGroup());
        p.push_back (choice ("gr_follow", "FOLLOW", { "NOTES", "FIXED" }, 0).engineGroup());
        p.push_back (choice ("gr_sync", "SYNC", { "OFF", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 0).engineGroup());
        // (SF-000) SAMPLE's own, with PLAY and KNOBS.
        //   NOTES       PITCH: a note plays the sample at its pitch. SLICES: a note chooses a slice of it (the parts its own
        //               onsets cut START..END into, or eight equal parts where it has none): C3 the first, each key above
        //               it the next, round again past the last. A slice plays at the pitch it was recorded at; TUNE shifts it
        //   LOOP        PLAY HELD: what plays (START..END, or the slice) goes round for as long as the note is held
        //   CROSSFADE   how much of the loop's end fades into its start: none (a hard join) to half of it
        //   VEL START   a soft note starts further in: at full, the softest half way through. A hard note from the start
        //   RANDOM START  each note starts somewhere else: none, to almost anywhere in what plays
        p.push_back (choice ("smp_notes", "NOTES", { "PITCH", "SLICES" }, 0).noPreset().sampleGroup());
        p.push_back (toggle ("smp_loop", "LOOP", false).noPreset().sampleGroup());
        p.push_back (pct ("smp_xfade", "CROSSFADE", 0.25f).noPreset().sampleGroup());
        p.push_back (pct ("smp_vel", "VEL START", 0.0f).noPreset().sampleGroup());
        p.push_back (pct ("smp_rand", "RANDOM START", 0.0f).noPreset().sampleGroup());
        p = withCuts (std::move (p));        // (SF-000) the slice markers
        jassert ((int) p.size() == kParams);
        return p;
    }
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override
    {
        // One sample for the plugin: MODEL's and LAYER MODEL's (and GRANULAR's and WAVETABLE's, where there are).
        const bool grains = std::any_of (models.begin(), models.end(), [] (const SynthModel& m) { return m.engine == E_GRAIN; });
        return grains ? "DROP A SAMPLE: FOR SAMPLE, GRANULAR, WAVETABLE" : "DROP SAMPLE (MODEL OR LAYER MODEL: SAMPLE)";      // (SF-000) short enough to be read beside RESAMPLE and LOAD
    }
    void loadFiles (const juce::StringArray& f) override { if (f.size() > 0) { sample.set (loadSampleFile (juce::File (f[0]), sr, 300.0)); buildFrames(); } }      // (SF-000) five minutes of a file, as VOICE has (it was one): a field recording to cut slices and grains from
    void save (juce::ValueTree& t) const override { sample.save (t, "SAMPLE"); }
    void load (const juce::ValueTree& t) override { sample.load (t, "SAMPLE", sr); buildFrames(); }
    bool hasWavetable() const { return std::any_of (models.begin(), models.end(), [] (const SynthModel& m) { return m.engine == E_WAVETABLE; }); }

    // WAVETABLE's TABLE SAMPLE [Nov 10]: eight frames from the loaded sample, at the voices' rate,
    // from evenly across it, start to end. Each frame is four periods of the sound where it is cut:
    // the period measured there (normalised autocorrelation, 25 Hz-2 kHz, the shortest lag within
    // 10 % of the best so an octave below isn't taken, refined between lags); where nothing is
    // periodic enough, the sample's analysed root or 1024 samples at 48 kHz. Hann-windowed, read as
    // harmonics (Mipmap::harmonicsOfCycles: a cycle that doesn't join up with itself doesn't buzz),
    // turned so every frame's fundamental starts at the same phase (frames blend without
    // cancelling), brought to one loudness; a frame 40 dB under the loudest (silence at an end)
    // takes its neighbour's.
    struct SampleFrames { std::vector<Mipmap> f; };
    static constexpr int kSampleFrames = 8;
    static double periodAt (const std::vector<float>& x, int t0, int w, double sr)
    {
        int n = 1; while (n < 2 * w) n <<= 1;
        std::vector<std::complex<double>> X ((size_t) n);
        for (int i = 0; i < w; ++i) X[(size_t) i] = x[(size_t) (t0 + i)];
        Mipmap::transform (X, false);
        for (auto& v : X) v = std::norm (v);
        Mipmap::transform (X, true);
        std::vector<double> c ((size_t) w + 1, 0.0);
        for (int i = 0; i < w; ++i) c[(size_t) i + 1] = c[(size_t) i] + (double) x[(size_t) (t0 + i)] * x[(size_t) (t0 + i)];
        const int lo = std::max (2, (int) (sr / 2000.0)), hi = std::min (w / 2, (int) (sr / 25.0));
        if (hi <= lo + 2 || c[(size_t) w] < 1.0e-9) return 0.0;
        std::vector<double> r ((size_t) hi + 2, 0.0);
        double best = 0.0;
        for (int lag = lo - 1; lag <= hi + 1; ++lag)
        {
            const double e = std::sqrt ((c[(size_t) (w - lag)] - c[0]) * (c[(size_t) w] - c[(size_t) lag])) + 1.0e-20;
            r[(size_t) lag] = X[(size_t) lag].real() / (double) n / e;
            if (lag >= lo && lag <= hi) best = std::max (best, r[(size_t) lag]);
        }
        if (best < 0.6) return 0.0;
        for (int lag = lo; lag <= hi; ++lag)
            if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
            {
                const double a = r[(size_t) lag - 1], b = r[(size_t) lag], d = r[(size_t) lag + 1], den = a - 2.0 * b + d;
                return lag + (std::abs (den) > 1.0e-12 ? 0.5 * (a - d) / den : 0.0);
            }
        return 0.0;
    }
    static std::shared_ptr<const SampleFrames> framesFrom (const SampleData& s, double voiceRate)
    {
        const int len = s.length(), chans = std::max (1, s.buf.getNumChannels());
        constexpr int kN = 2048, kCycles = 4;
        const double fallback = s.rootHz > 20.0f ? s.sr / (double) s.rootHz : 1024.0 * s.sr / 48000.0;
        const int win = (int) (0.085 * s.sr);
        if (len < (int) (fallback * kCycles) + win + 8) return nullptr;
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i) { float v = 0.0f; for (int ch = 0; ch < chans; ++ch) v += s.buf.getSample (ch, i); mono[(size_t) i] = v / (float) chans; }
        auto at = [&mono, len] (double t)
        {
            const int i = juce::jlimit (1, len - 3, (int) t);
            const float fr = (float) (t - i), xm1 = mono[(size_t) i - 1], x0 = mono[(size_t) i], x1 = mono[(size_t) i + 1], x2 = mono[(size_t) i + 2];
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return (double) (((c3 * fr + c2) * fr + c1) * fr + x0);
        };
        std::vector<std::vector<float>> amps ((size_t) kSampleFrames), phs ((size_t) kSampleFrames);
        std::vector<double> rms ((size_t) kSampleFrames, 0.0);
        std::vector<double> x ((size_t) (kN * kCycles));
        const double usable = (double) len - 8.0 - win - fallback * kCycles;
        for (int f = 0; f < kSampleFrames; ++f)
        {
            const double t0 = 2.0 + std::max (0.0, usable) * f / (kSampleFrames - 1);
            double per = periodAt (mono, (int) t0, win, s.sr);
            if (per <= 0.0) per = fallback;
            per = std::min (per, ((double) len - 8.0 - t0) / kCycles);
            for (size_t i = 0; i < x.size(); ++i) x[i] = at (t0 + ((double) i + 0.5) * per / kN);
            auto& amp = amps[(size_t) f]; auto& ph = phs[(size_t) f];
            Mipmap::harmonicsOfCycles (x, kCycles, Mipmap::kSize / 4, amp, ph);
            double e = 0.0;
            for (auto a : amp) e += 0.5 * (double) a * a;
            rms[(size_t) f] = std::sqrt (e);
            const float p1 = ph[0];
            for (size_t h = 0; h < ph.size(); ++h) ph[h] -= (float) (h + 1) * p1;
        }
        const double loud = *std::max_element (rms.begin(), rms.end());
        if (loud < 1.0e-6) return nullptr;
        // Silent frames take a neighbour's (the nearest loud one before, else after).
        for (int f = 0; f < kSampleFrames; ++f)
            if (rms[(size_t) f] < 0.01 * loud)
                for (int d = 1; d < kSampleFrames; ++d)
                {
                    const int k = f - d >= 0 && rms[(size_t) (f - d)] >= 0.01 * loud ? f - d : f + d < kSampleFrames && rms[(size_t) (f + d)] >= 0.01 * loud ? f + d : -1;
                    if (k >= 0) { amps[(size_t) f] = amps[(size_t) k]; phs[(size_t) f] = phs[(size_t) k]; rms[(size_t) f] = rms[(size_t) k]; break; }
                }
        auto out = std::make_shared<SampleFrames>();
        out->f.resize ((size_t) kSampleFrames);
        for (int f = 0; f < kSampleFrames; ++f)
        {
            const float g = (float) (0.45 / rms[(size_t) f]);
            for (auto& a : amps[(size_t) f]) a *= g;
            out->f[(size_t) f].build (amps[(size_t) f], phs[(size_t) f], voiceRate);
        }
        return out;
    }
    void buildFrames()
    {
        if (! hasWavetable()) return;
        auto s = sample.get();
        frames.set (s != nullptr ? framesFrom (*s, 2.0 * sr) : nullptr);
    }

    void prepare (double rate, int mb) override
    {
        if (auto s = sample.get(); s != nullptr && std::abs (s->sr - rate) > 1 && s->flac.getSize() > 0) sample.set (loadSampleFlac (s->flac, s->name, rate, s->flacGain));
        const bool rateChanged = std::abs (sr - rate) > 1.0;
        sr = rate;
        osr = 2.0 * rate;
        if (rateChanged || (frames.audio() == nullptr && sample.get() != nullptr)) buildFrames();
        waves = WaveSet::get (osr);
        dec.prepare (rate);
        decB.prepare (rate);
        for (auto& d : dec4) d.set (HalfbandDesign::forStage (rate, 1));
        for (auto& d : dec4b) d.set (HalfbandDesign::forStage (rate, 1));
        split.prepare (rate);
        // DRIFT: each voice slot's own tuning, cutoff and pace, the same every time (its own generator).
        Noise dr { 0x5BD1E995u };
        for (auto& d : slotDrift) { d.tune = dr.next(); d.cut = dr.next(); d.ph = dr.uni(); d.rate = 0.7f + 0.6f * dr.uni(); }
        out.prepare (rate, mb, roleId, name.c_str(), true);
        ens.prepare (rate);
        events.reserve (2048);
        held.reserve (128);
        buildGrainSource();
        reset();
    }
    void reset() override
    {
        echoes.reset();
        for (auto& v : voices) v.active = false;
        held.clear();
        numDown = 0;
        dec.reset();
        for (auto& d : dec4) d.reset();
        decB.reset();
        for (auto& d : dec4b) d.reset();
        bLive = 0;
        split.reset();
        driftClock = 0.0;
        ens.reset();
        out.reset();
        activeVoices = 0;
        acidCap[0] = acidCap[1] = 0.0f;
    }

    // Control values of a voice, computed on the stream's 32-sample grid (and at a note start),
    // so the voice sounds the same at any host buffer size.
    struct VoiceCtl
    {
        float res = 0, driveG = 1, driveNorm = 1, driveW = 1, color = 0.5f, det = 0.5f, cutT = 0.5f, gainAll = 0, subMul = 1, pw2 = 0.5f, osc2Amt = 0, subLvl = 0, sideK = 1;
        float detCents = 0, uniR[8] {}, pw[8] {}, r2[8] {};
        // (SF-000) GRANULAR's knobs for the block: the place, the pace of SCAN (the source's samples a sample), a grain's
        // length (seconds), how many overlap, the spray (of the source's length), the pitch spread, the shape, how many backwards
        float grPos = 0, grSpeed = 0.05f, grLenS = 0.155f, grOv = 4.0f, grSpray = 0.15f, grPitch = 0.25f, grShape = 0.5f, grRev = 0.0f;
        int grClock = 0, grSpread = 0, grFollow = 0, grSync = 0;
        int uni = 1, shape1 = 0, oct1[8] {}, oct2[8] {}, octSub = 0;
        bool doSub = false;
        // ACID 303-TYPE: the cutoff's log (natural) at rest and at the envelope's peak, the accent
        // sweep's reach (same units), RESONANCE, its output gain (the pot's second gang), the
        // envelope's and the sweep's coefficients, the square's share.
        float acidL0 = 0, acidL1 = 0, acidSweep = 0, acidRes = 0, acidFbK = 0, acidComp = 1, megK = 1, megKAcc = 1, capC = 0, capD = 1, sqTarget = 0, sqFade = 0, sqHpK = 1;
        // FM 4-OP: the algorithm, each operator's ratio, level, envelope step (per sample) and
        // floor; the depth a modulator's full level gives (radians); D's feedback; the carriers' scale.
        int fmAlgo = 3;
        float fmRatio[4] {}, fmLevel[4] {}, fmMul[4] {}, fmHold[4] {}, fmBeta = 3.0f, fmFb = 0.0f, fmOut = 1.0f;
    };
    struct Voice
    {
        bool active = false, releasing = false;
        int note = 60, gen = 0, model = 0, layer = 0, autoOff = -1;
        bool through = false;             // (SF-000) SAMPLE, PLAY THROUGH: letting the note go does not end it
        bool shaped = false;              // (SF-000) SAMPLE, KNOBS on: the lane's knobs act on it
        double spos2 = 0.0, cutPos = 0.0; // (SF-000) the second, detuned reading's place; THROUGH: where GATE ends it (0: at its END)
        float sliceHz = 0.0f;             // (SF-000) SAMPLE, NOTES SLICES: the note's own pitch, which the rate is taken against (0: the sample's root)
        double loopX = 0.0;               // (SF-000) SAMPLE, LOOP: the length of the crossfade at the loop's end (0: no loop)
        bool looped = false;              // (SF-000) and it has gone round once (its START's fade is behind it)
        float tilt[2] {};                 // (SF-000) COLOR's tilt on a sample: the low part of each side
        float vel = 1.0f, gain = 1.0f, freq = 110.0f, target = 110.0f, glideCoef = 0.0f, accent = 0.0f;
        float vel0 = 1.0f;                // the note's own velocity (0..1), to play it again on a MODEL change
        float pitch = 60.0f, pitchTuned = 60.0f;   // the note (with chord interval) and the tuned pitch it plays at
        double rate = 96000.0;            // render rate: 2x, or 4x (os: setupVoice)
        int os = 2;
        double ph[8] {}, ph2[8] {}, subPh = 0.0, modPh = 0.0, spos = 0.0;
        float pan[8] {};
        Adsr amp, fenv;
        SyncSaw sync, sync2;              // (SF-000) sync2: DETUNE's second
        AdaaTanh driveSat, ms20Sat;
        bool driveOn = false;
        VoiceCtl ctl;
        bool ctlStale = true;
        Ladder ladder;
        DiodeLadder303 dl;                // ACID 303-TYPE: its filter, envelope and square (kept on a retrigger)
        float meg = 0.0f, sqMix = 0.0f, sqHp = 0.0f, sqPrev = 0.0f;
        float warpT = 0.0f, warpPh = 0.0f, warpRate = 1.0f;     // WARP's wow and flutter: this voice's time, phase, pace
        double fmPh[4] {};                // FM 4-OP: the operators' phases (from 0 at the note), envelopes,
        float fmEnv[4] { 1, 1, 1, 1 }, fmPrev[2] {};   // and D's last two outputs (its feedback)
        Svf svf, svf2, nbp, nbp2;         // (SF-000) nbp2: NOISE's second band, DETUNE's
        Svf svfS;                         // (SF-000) SAMPLE and GRANULAR on a stereo file: the filter again, for the left less the right
        float t = 0.0f, fb = 0.0f, drift = 0.0f;
        // (SF-000) lv, sc: the copy of the source it reads (-1 the source itself, else that octave-down copy) and that copy's
        // samples for one of the source's; wc, ws, rc, rs: its window, turned a step a sample (see E_GRAIN)
        struct Grain { bool on = false; double pos = 0, rate = 1; int len = 0, age = 0; float pan = 0.5f; SampleRegion reg;
                       int lv = -1; double sc = 1.0, wc = 1.0, ws = 0.0, rc = 1.0, rs = 0.0; } grains[12];   // reg: where it started (end 0: the whole source)
        int grainClock = 0;
        double grScan = 0.0;              // (SF-000) how far SCAN has carried the place since the note began, in the source's samples
        double grBeat = 0.0;              // the song's place at this sample, in beats, and the last tick of CLOCK that was looked at
        long long grTick = -1;
        SampleRegion reg;                 // SAMPLE: the part of the sample this note plays
        uint64_t born = 0;                // renderTick when it started: one that hasn't sounded yet is never stolen
        float level() const { return active ? amp.v * vel * gain : 0.0f; }
    };

    // ACID and WARP push the knobs they move (as a macro moves a knob), for everything this block
    // reads: c.p and c.p0 point at the pushed copies while the voices run.
    //   ACID: RESONANCE to 95 %, ENV AMT to 90 %, ACCENT to full (each from where it is, never down),
    //         DRIVE + 35 % and CUTOFF + 10 %: the acid squelch, on any model; the 303's own.
    //   WARP: DETUNE (60 % of the way up) and SPACE (half way): more spread, more ensemble; and in
    //         updateControls tape wow and flutter, each voice on its own phase and pace. On GRANULAR
    //         DETUNE is the grains' scatter and pitch spread: the texture smears.
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>* side, juce::AudioBuffer<float>* extra, juce::MidiBuffer& midi) override
    {
        const float* p = c.p; const float* p0 = c.p0;
        std::copy (p, p + kParams, pushed); std::copy (p0, p0 + kParams, pushed0);
        push (pushed); push (pushed0);
        c.p = pushed; c.p0 = pushed0;
        processVoices (c, io, side, extra, midi);
        c.p = p; c.p0 = p0;
    }
    static void push (float* v)
    {
        auto toward = [] (float& x, float to, float a) { if (x < to) x += (to - x) * a; };
        if (const float acid = clamp01 (v[ACID]); acid > 0.0f)
        {
            toward (v[RES], 0.95f, acid); toward (v[ENVAMT], 0.9f, acid); toward (v[ACCENT], 1.0f, acid);
            v[DRIVE] = std::min (1.0f, v[DRIVE] + 0.35f * acid);
            v[CUTOFF] = std::min (1.0f, v[CUTOFF] + 0.1f * acid);
        }
        if (const float warp = clamp01 (v[WARP]); warp > 0.0f)
        {
            toward (v[DETUNE], 1.0f, 0.6f * warp); toward (v[SPACE], 1.0f, 0.5f * warp);
            // (SF-000) GRANULAR's scatter and pitch spread have knobs of their own now: WARP smears by those, as far as it
            // did by DETUNE (SPRAY's square is how much of the source, three tenths of it at DETUNE's top)
            float d = std::min (1.0f, v[GRSPRAY] * v[GRSPRAY] / 0.3f);
            toward (d, 1.0f, 0.6f * warp);
            v[GRSPRAY] = std::max (v[GRSPRAY], std::sqrt (0.3f * d));
            toward (v[GRPITCH], 0.5f, 0.6f * warp);
        }
    }
    void processVoices (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midi)
    {
        echoes.run (c, midi, events, c.v (DENS), echoSpaceBeats (c.ci (ESPACE)), c.v (ESWING), 1.0f, true);
        const SampleData* smp = sample.audio();
        // The sample's START..END now: a SAMPLE note started in this block plays it, GRANULAR's
        // grains come from it.
        blockReg = smp != nullptr ? regionFrom (c, SSTART, *smp, SGAIN) : SampleRegion();
        for (auto& v : voices) if (v.active) v.reg.level = blockReg.level;        // (SF-000) GAIN reaches a note that is sounding
        // WAVETABLE's frames from the sample, if TABLE says so and there are some.
        tableFrames = c.ci (TABLE) == 1 ? frames.audio() : nullptr;
        if (tableFrames != nullptr && tableFrames->f.size() < (size_t) kSampleFrames) tableFrames = nullptr;
        pitchOffset = c.v (TUNE) + c.bend + (c.b (KEYFOLLOW) ? (float) keyOffset (c.keyRoot) : 0.0f);      // (SF-000: and the pitch wheel)
        const int modelA = c.ci (MODEL), modelB = c.ci (LAYERMODEL);
        ensembleAmt = models[(size_t) juce::jlimit (0, (int) models.size() - 1, modelA)].ensemble;
        if (const auto& ma = models[(size_t) juce::jlimit (0, (int) models.size() - 1, modelA)]; ma.engine == E_SAMPLE && ! ma.mono && c.v (SENV) > 0.5f) ensembleAmt = 0.3f;      // (SF-000) SPACE on a sample
        const float layer = c.v (LAYER);
        int autoOff; float relMul;
        gateShape (c.v (GATE), c.samplesPerBeat(), autoOff, relMul);
        if (modelA != lastModel)
        {
            // MODEL change, at once: what sounds releases on the old model (its own release, so a
            // mono/poly switch can't leave a voice hanging), and the keys still down start again on
            // the new one, in the order they went down, each at its own velocity: a held chord goes
            // on in the new sound. The keys come from the ledger of keys down (down), not from the
            // voices: a voice's place in the pool says nothing about when its key went down (a
            // freed voice is reused by a newer key). Echoes don't come back.
            lastModel = modelA;
            for (auto& v : voices) if (v.active) { v.amp.off(); v.fenv.off(); v.releasing = true; }
            held.clear();
            // A mono model plays the newest key; the others go back on its stack (no slide through them).
            const bool monoA = models[(size_t) juce::jlimit (0, (int) models.size() - 1, modelA)].mono;
            const bool monoB = models[(size_t) juce::jlimit (0, (int) models.size() - 1, modelB)].mono;
            restarting = true;
            for (int k = 0; k < numDown; ++k)
            {
                const bool newest = k == numDown - 1;
                NoteEvent ev;
                ev.note = down[(size_t) k].note; ev.vel = down[(size_t) k].vel;
                if (monoA && ! newest) held.push_back (ev.note);
                else noteOn (c, modelA, ev, 1.0f / (1.0f + layer * 0.5f), 0, autoOff, relMul);
                if (layer > 0.01f && (! monoB || newest)) noteOn (c, modelB, ev, layer / (1.0f + layer * 0.5f), 1, autoOff, relMul);
            }
            restarting = false;
        }
        size_t e = 0;
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        // LAYER SPLIT: SPLIT across the block; MODEL's gain against the layer as noteOn gives it.
        const float k0 = c.v0 (SPLIT), k1 = c.v (SPLIT);
        const float sv0 = LayerSplit::amount (k0), sv1 = LayerSplit::amount (k1);
        split.set (c.v (SPLITHZ), 1.0f / (1.0f + layer * 0.5f), sv1);
        for (int i0 = 0; i0 < c.n;)
        {
            segPpq = c.ppq + (double) i0 * c.bpm / (60.0 * c.sr);        // (SF-000) for a note that starts in it

            // Sub-blocks end at the next event: every note starts on its own sample.
            while (e < events.size() && events[e].offset <= i0)
            {
                const auto& ev = events[e++];
                if (ev.gen == 0)                       // the keys down (echoes aren't keys)
                {
                    if (ev.on) keyDown (ev.note, ev.vel);
                    else if (ev.note < 0) numDown = 0;
                    else keyUp (ev.note);
                }
                if (ev.on)
                {
                    // ev.gain: the DENSITY fade, applied after the velocity response (v.gain is post-filter).
                    noteOn (c, modelA, ev, ev.gain / (1.0f + layer * 0.5f), 0, autoOff, relMul);
                    if (layer > 0.01f) noteOn (c, modelB, ev, ev.gain * layer / (1.0f + layer * 0.5f), 1, autoOff, relMul);
                }
                else noteOff (ev.note, ev.gen, relMul);
            }
            // Sub-blocks also sit on a fixed grid of the stream (block-size independent sound).
            int n = std::min (kSub - (int) ((c.samplePos + i0) % kSub), c.n - i0);
            if (e < events.size()) n = std::min (n, std::max (1, events[e].offset - i0));
            std::fill (L2, L2 + 2 * n, 0.0f);
            std::fill (R2, R2 + 2 * n, 0.0f);
            // The layer's voices sum on their own (LAYER SPLIT), down through their own halfbands
            // while any sounds and for kBDrain sub-blocks after (the filters ring out into the sum).
            bool anyB = false;
            for (auto& v : voices) if (v.active && v.layer == 1) { anyB = true; break; }
            if (anyB) bLive = kBDrain;
            const bool runB = bLive > 0;
            if (runB)
            {
                std::fill (Lb2, Lb2 + 2 * n, 0.0f); std::fill (Rb2, Rb2 + 2 * n, 0.0f);
                std::fill (Lb4, Lb4 + 4 * n, 0.0f); std::fill (Rb4, Rb4 + 4 * n, 0.0f);
            }
            bool any4 = false;
            const bool onGrid = (c.samplePos + i0) % kSub == 0;
            acidOwned[0] = acidOwned[1] = false;
            for (auto& v : voices)
                if (v.active)
                {
                    const bool b = v.layer == 1;
                    if (v.os == 4)
                    {
                        if (! b && ! any4) { std::fill (L4, L4 + 4 * n, 0.0f); std::fill (R4, R4 + 4 * n, 0.0f); any4 = true; }
                        render (c, v, b ? Lb4 : L4, b ? Rb4 : R4, 4 * n, smp, onGrid);
                    }
                    else render (c, v, b ? Lb2 : L2, b ? Rb2 : R2, 2 * n, smp, onGrid);
                }
            // 4x voices: down to 2x (always run, so the filters' state stays continuous).
            if (! any4) { std::fill (L4, L4 + 4 * n, 0.0f); std::fill (R4, R4 + 4 * n, 0.0f); }
            for (int i = 0; i < 2 * n; ++i)
            {
                L2[i] += dec4[0].down (L4[2 * i], L4[2 * i + 1]);
                R2[i] += dec4[1].down (R4[2 * i], R4[2 * i + 1]);
            }
            dec.process (0, L2, L + i0, n);
            dec.process (1, R2, R + i0, n);
            if (runB)
            {
                for (int i = 0; i < 2 * n; ++i)
                {
                    Lb2[i] += dec4b[0].down (Lb4[2 * i], Lb4[2 * i + 1]);
                    Rb2[i] += dec4b[1].down (Rb4[2 * i], Rb4[2 * i + 1]);
                }
                decB.process (0, Lb2, Lb, n);
                decB.process (1, Rb2, Rb, n);
                if (! anyB && --bLive == 0) { decB.reset(); for (auto& d : dec4b) d.reset(); }
            }
            // The two layers together: split, or (off) their plain sum.
            if (! split.off (sv0, sv1))
                for (int i = 0; i < n; ++i)
                    split.tick (L[i0 + i], R[i0 + i], runB ? Lb[i] : 0.0f, runB ? Rb[i] : 0.0f,
                                LayerSplit::amount (k0 + (k1 - k0) * (float) (i0 + i) / (float) c.n));
            else if (runB)
                for (int i = 0; i < n; ++i) { L[i0 + i] += Lb[i]; R[i0 + i] += Rb[i]; }
            // ACID 303-TYPE's accent sweep drains while no note of its holds it.
            for (int l = 0; l < 2; ++l)
                if (! acidOwned[l] && acidCap[l] > 0.0f)
                    acidCap[l] *= std::exp (-(float) n / (acidDrainSeconds (clamp01 (c.v (RES))) * (float) sr));
            ++renderTick;                              // every voice started before now has sounded
            i0 += n;
        }
        ens.process (L, R, c.n, ensembleAmt * c.v (SPACE) * 2.0f);
        split.monoLows (L, R, c.n, k0, k1);
        driftClock += (double) c.n / sr;
        out.process (c, io, FILT, -1, c.v (PDEPTH), c.v (PSHAPE), c.b (ALLOCON), LEVEL);
        int act = 0; for (auto& v : voices) act += v.active ? 1 : 0;
        activeVoices = act;
    }

    // VOICES and the pocket; under them, with a sample loaded, the sample (its START and END to drag).
    int displayHeight() const override { return kOwn + (sample.get() != nullptr ? SampleStrip::kHeight : 0); }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto s = sample.get();
        const auto st = SampleStrip::frameIn (r, kOwn);
        if (s != nullptr && ! st.isEmpty()) strip.paint (g, st, *s, *this, SSTART, SCUTS);
        r = r.withHeight ((float) kOwn);
        auto left = r.removeFromLeft (r.getWidth() * 0.4f);
        ui::frame (g, left, "VOICES");
        // Playing has kPlayVoices; more sound only while a MODEL change lets the old sound release.
        const int nv = activeVoices.load();
        ui::label (g, juce::String (nv) + (nv <= kPlayVoices ? " / " + juce::String (kPlayVoices) : juce::String()), left.reduced (10, 18), ui::kText, 18.0f, juce::Justification::centred);
        // The sample's name is on the strip under this, once the editor has made room for it.
        const juce::String root = s != nullptr && s->rootHz > 0 ? "ROOT " + juce::MidiMessage::getMidiNoteName ((int) std::lround (s->rootNote), true, true, 3) : juce::String();
        ui::label (g, s == nullptr ? juce::String ("NO SAMPLE") : st.isEmpty() ? s->name.toUpperCase() + (root.isEmpty() ? "" : " " + root) : root,
                   left.reduced (8, 2).removeFromBottom (14), ui::kFaint, 10.0f);
        out.pocket.paint (g, r.withTrimmedLeft (8));
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool press) override
    {
        if (auto smp = sample.get(); smp != nullptr) { if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty()) { strip.mouseKit (p, SampleStrip::wave (st), press, *this, SSTART, SCUTS, *smp); return; } }
        if (press) strip.up();
    }
    void displayMouseUp() override { strip.up(); }
    void displayDouble (juce::Point<float> p, juce::Rectangle<float> r) override
    {
        if (auto smp = sample.get(); smp != nullptr) if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty()) SampleStrip::doubleKit (p, SampleStrip::wave (st), *this, SSTART, SCUTS, *smp);
    }

private:
    void noteOn (Ctx& c, int modelIdx, const NoteEvent& ev, float gain, int layerId, int autoOff, float relMul)
    {
        const auto& m = models[(size_t) juce::jlimit (0, (int) models.size() - 1, modelIdx)];
        int chordIdx = c.ci (CHORD);
        const auto& chord = chordIdx == 0 ? chordTable()[(size_t) m.chord] : chordTable()[(size_t) std::max (0, chordIdx - 1)];
        const float tune = pitchOffset;
        const bool accent = ev.vel > 0.85f && ev.gen == 0;
        if (m.mono)
        {
            // Mono: legato slides (acid), else retrigger. Note stack for releases.
            // DENSITY echoes never interrupt a note that is still held.
            if (ev.gen > 0 && ! held.empty()) return;
            if (layerId == 0 && ev.gen == 0 && held.size() < 120) held.push_back (ev.note);
            Voice* v = nullptr;
            for (auto& x : voices) if (x.active && x.layer == layerId && x.model == modelIdx && ! x.releasing) v = &x;
            const bool legato = v != nullptr && ev.gen == 0 && held.size() > 1;
            // ACID 303-TYPE: one VCO, VCF and VCA, so a new note takes the voice still releasing.
            if (v == nullptr && m.acid)
                for (auto& x : voices) if (x.active && x.layer == layerId && x.model == modelIdx) v = &x;
            if (v == nullptr)
            {
                const int i = allocVoice (ev.gen);
                if (i < 0) return;
                v = &voices[(size_t) i];
            }
            setupVoice (c, *v, m, modelIdx, ev, (float) ev.note + tune, gain, layerId, autoOff, relMul, ! legato, accent);
            if (legato) v->glideCoef = std::exp (-1.0f / (0.06f * (float) v->rate * (m.acid ? 1.0f : 0.5f + c.v (GLIDE) * 3.0f)));
            return;
        }
        for (size_t k = 0; k < chord.size(); ++k)
        {
            const int i = allocVoice (ev.gen);
            if (i < 0) return;                         // no voice for it (allocVoice)
            auto& v = voices[(size_t) i];
            setupVoice (c, v, m, modelIdx, ev, (float) (ev.note + chord[k]) + tune, gain / std::sqrt ((float) chord.size()), layerId, autoOff, relMul, true, accent);
        }
    }

    void setupVoice (Ctx& c, Voice& v, const SynthModel& m, int modelIdx, const NoteEvent& ev, float note, float gain, int layerId,
                     int autoOff, float relMul, bool retrigger, bool accent)
    {
        const bool wasActive = v.active && ! v.releasing;
        if (retrigger || ! wasActive)
        {
            const float keepFreq = v.freq;
            // ACID 303-TYPE retriggers its one VCO, VCF and VCA as they are: the filter rings on, the
            // oscillator runs on, the level rises from where it is (no click, as on the 303).
            const bool acidSame = m.acid && v.active && v.model == modelIdx;
            const DiodeLadder303 keepDl = v.dl;
            const Adsr keepAmp = v.amp;
            const double keepPh = v.ph[0];
            const float keepSq[3] = { v.sqMix, v.sqHp, v.sqPrev };
            v = Voice();
            v.reg = blockReg;                  // SAMPLE: from START (a legato slide plays on where it is)
            v.spos = v.reg.start;
            // 4x: the driven ladders and MS20, the 303's filter, hard sync, and FM [Nov 10] (a high
            // bell's sidebands reach past 48 kHz: at 2x they folded back into the audible band, 45 dB
            // down at C7; SFTests --alias).
            v.os = (m.filt == F_LADDER || m.filt == F_MS20 || m.filt == F_DIODE || m.engine == E_SYNC || m.engine == E_FM || m.engine == E_FM4) ? 4 : 2;
            v.rate = sr * v.os;
            v.freq = wasActive ? keepFreq : noteHz (note);
            for (int k = 0; k < 8; ++k) { v.ph[k] = rng.uni(); v.ph2[k] = rng.uni(); v.pan[k] = 0.5f + (k == 0 ? 0.0f : ((k & 1) ? -0.35f : 0.35f) * (0.5f + 0.5f * rng.uni())); }
            v.warpPh = warpRng.uni(); v.warpRate = 0.8f + 0.45f * warpRng.uni();   // its own generator: WARP leaves every other random draw where it was
            if (acidSame)
            {
                v.dl = keepDl; v.amp = keepAmp; v.ph[0] = keepPh;
                v.sqMix = keepSq[0]; v.sqHp = keepSq[1]; v.sqPrev = keepSq[2];
            }
            else if (m.acid)
            {
                v.dl.setHighPass (DiodeLadder303::kHighPass, v.rate);
                v.sqMix = c.v (COLOR) < 0.5f ? 0.0f : 1.0f;
            }
            if (m.acid) v.meg = 1.0f;          // the 303's envelope: up at once, on every note it isn't slid into
        }
        v.active = true; v.releasing = false;
        v.note = ev.note; v.gen = ev.gen; v.model = modelIdx; v.layer = layerId;
        v.vel = m.acid ? 1.0f : 0.55f + 0.45f * lerp (1.0f, ev.vel, 0.6f);   // the 303 knows no velocity, only accent
        v.vel0 = ev.vel;
        v.gain = gain;
        v.target = noteHz (note);
        v.pitchTuned = note; v.pitch = note - pitchOffset;
        v.glideCoef = c.v (GLIDE) > 0.02f && ! m.acid ? std::exp (-1.0f / ((0.005f + c.v (GLIDE) * 0.4f) * (float) v.rate)) : 0.0f;
        if (! wasActive) v.freq = v.target;
        v.accent = accent ? c.v (ACCENT) : 0.0f;
        const float decK = std::pow (2.0f, (c.v (DECAY) - 0.5f) * 4.0f) * std::pow (0.6f, (float) ev.gen) * (1.0f - v.accent * 0.4f);
        const float attK = c.v (ATTACK) * 2.0f;
        // (SF-000) a sample with ENVELOPE on: DECAY is its amp decay (0.15 to 2.4 s, to silence; at full, none), and
        // ENV AMT sweeps its filter (updateControls). PLAY THROUGH: the note's end does not end it.
        v.shaped = m.engine == E_SAMPLE && c.v (SENV) > 0.5f;
        v.through = m.engine == E_SAMPLE && c.ci (SPLAY) == 1;
        // (SF-000) SAMPLE: the slice its note chooses, where in it this note starts, and the loop. Only as a note starts
        // from START (a legato slide plays on where it is).
        if (m.engine == E_SAMPLE && (retrigger || ! wasActive))
            if (const SampleData* s = sample.audio(); s != nullptr && v.reg.end > v.reg.start)
            {
                if (c.ci (SNOTES) == 1)
                {
                    const Cuts cuts = cutsFrom (c, SCUTS, *s);
                    v.reg = sliceRegion (*s, v.reg, ev.note - 60, &cuts);
                    v.sliceHz = noteHz (ev.note);
                    v.freq = v.target;                  // a slice does not glide in from the note before
                }
                const double len = v.reg.end - v.reg.start;
                const float rnd = c.v (SRANDSTART), soft = c.v (SVELSTART) * (1.0f - clamp01 (ev.vel));
                double in = 0.0;
                if (soft > 0.001f) in += 0.5 * (double) soft * len;
                if (rnd > 0.001f) in += (double) (rnd * rnd * 0.9f * smpRng.uni()) * (len - in);
                if (in > 1.0)
                {
                    v.reg.start += std::min (in, len - 64.0);
                    v.reg.fadeIn = std::min (std::max (v.reg.fadeIn, 0.002 * s->sr), 0.5 * (v.reg.end - v.reg.start));
                    if (v.reg.fadeIn + v.reg.fadeOut > v.reg.end - v.reg.start) v.reg.fadeOut = v.reg.end - v.reg.start - v.reg.fadeIn;
                }
                v.spos = v.reg.start;
                if (c.b (SLOOP) && ! v.through && v.reg.end - v.reg.start > 256.0)
                {
                    v.loopX = std::max (16.0, 0.5 * (double) c.v (SXFADE) * (v.reg.end - v.reg.start));
                    v.reg.fadeOut = 0.0;                // it does not reach its END: it goes round before it
                }
            }
        // (SF-000) an echo of a slow model comes up in a third of the time: a pad's own attack is as long as the gap between
        // echoes, and DENSITY was all but unheard on it
        const float echoAtt = ev.gen > 0 && m.att >= 0.2f ? 0.3f : 1.0f;
        if (v.shaped)
        {
            const float up = clamp01 ((c.v (DECAY) - 0.8f) * 5.0f);          // (the top fifth of DECAY: the sustain comes up to all of it)
            v.amp.set (m.att + attK, 0.6f * decK, up * up * (3.0f - 2.0f * up), m.rel * relMul, v.rate);
            v.cutPos = v.through && c.v (GATE) < 0.995f ? v.reg.start + (double) c.v (GATE) * (v.reg.end - v.reg.start) : 0.0;
        }
        else
        v.amp.set ((m.att + attK) * echoAtt, m.dec * (m.acid ? 1.0f : decK), m.sus, m.rel * relMul, v.rate);
        v.fenv.set (m.fAtt + attK * 0.5f, m.fDec * decK, m.fSus, m.fRel * relMul, v.rate);
        v.amp.on (retrigger || ! wasActive);
        v.fenv.on (retrigger || ! wasActive);
        v.autoOff = autoOff > 0 ? autoOff * v.os : autoOff;
        v.t = 0.0f;
        v.grScan = 0.0; v.grTick = -1; v.grBeat = segPpq;        // (SF-000)
        v.ctlStale = true;
        v.born = renderTick;
    }

    // A voice for a new note (gen: 0 played, 1.. a DENSITY echo): a free one; else one that is
    // releasing (the quietest); else an echo's (the quietest); else, for a played note only, the
    // oldest one held: an echo never takes a held key's voice (with the pool full of held notes it
    // is dropped). Never a voice started at this same instant and not yet heard (a chord's other
    // tones, the keys a MODEL change plays again): its level is still 0, the quietest of all, and
    // each new tone used to take the one before it, so of a full chord only the last sounded.
    // Playing uses kPlayVoices; a MODEL change (restarting) may use the whole pool, so the chord it
    // plays again doesn't cut off the release of the one it replaces.
    // -1: no voice for it (every one started at this instant, or held, for an echo): dropped.
    int allocVoice (int gen) const
    {
        int active = 0;
        for (const auto& v : voices) active += v.active ? 1 : 0;
        if (restarting || active < kPlayVoices)
            for (size_t i = 0; i < voices.size(); ++i) if (! voices[i].active) return (int) i;
        int best = -1, bestRank = 3;
        float bestLvl = 0.0f;
        uint64_t bestBorn = 0;
        for (size_t i = 0; i < voices.size(); ++i)
        {
            const auto& v = voices[i];
            if (! v.active || v.born == renderTick) continue;     // a free one past kPlayVoices isn't for playing
            const int rank = v.releasing ? 0 : v.gen > 0 ? 1 : 2;
            if (rank == 2 && gen > 0) continue;
            const float l = v.level();
            const bool better = rank < bestRank || (rank == bestRank && (rank < 2 ? l < bestLvl : v.born < bestBorn));
            if (better) { best = (int) i; bestRank = rank; bestLvl = l; bestBorn = v.born; }
        }
        return best;
    }

    // The keys down, in the order they went down, with their velocities (allocation-free).
    void keyDown (int note, float vel)
    {
        keyUp (note);                                  // a key played again is the newest
        if (numDown < (int) down.size()) down[(size_t) numDown++] = { note, vel };
    }
    void keyUp (int note)
    {
        int w = 0;
        for (int r = 0; r < numDown; ++r) if (down[(size_t) r].note != note) down[(size_t) w++] = down[(size_t) r];
        numDown = w;
    }

    void noteOff (int note, int gen, float relMul)
    {
        juce::ignoreUnused (relMul);
        if (note < 0) { for (auto& v : voices) { v.amp.off(); v.fenv.off(); v.releasing = true; } held.clear(); return; }
        if (gen == 0)
        {
            held.erase (std::remove (held.begin(), held.end(), note), held.end());
            for (auto& v : voices)
            {
                if (! v.active || v.releasing || v.note != note) continue;
                if (v.through) continue;                                    // (SF-000) it plays to its END
                const bool mono = models[(size_t) v.model].mono;
                if (mono && ! held.empty())
                {
                    // Mono: return to the previous held note (slide).
                    v.note = held.back();
                    v.target = noteHz ((float) held.back() + pitchOffset);
                    v.pitch = (float) held.back(); v.pitchTuned = v.pitch + pitchOffset;
                    v.glideCoef = std::exp (-1.0f / (0.06f * (float) v.rate));
                }
                // A played note's release ends its voice even if an echo retriggered it. (Poly
                // layer voices are released even when the mono layer slides back: they used to
                // be skipped, and stuck.)
                else if (v.gen == 0 || mono) { v.amp.off(); v.fenv.off(); v.releasing = true; }
            }
            return;
        }
        // Echo releases only end echo voices, never a voice a held note owns.
        for (auto& v : voices)
            if (v.active && ! v.releasing && ! v.through && v.note == note && v.gen == gen && ! (models[(size_t) v.model].mono && ! held.empty()))
            { v.amp.off(); v.fenv.off(); v.releasing = true; }
    }

    // Renders n samples at the voice's oversampled rate (v.rate) into L/R (accumulating).
    void updateControls (Ctx& c, Voice& v, int n)
    {
        const auto& m = models[(size_t) v.model];
        const auto& W = *waves;
        auto& k = v.ctl;
        const double vr = v.rate;
        const float invOsr = (float) (1.0 / vr);
        // TUNE and KEY reach notes that are already sounding (gliding there if GLIDE is set).
        if (const float tuned = v.pitch + pitchOffset; std::abs (tuned - v.pitchTuned) > 1e-4f) { v.pitchTuned = tuned; v.target = noteHz (tuned); }
        const float cutK = c.v (CUTOFF), envA = c.v (ENVAMT) * (v.shaped ? 1.0f : m.envMul) * (1.0f + v.accent);       // (SF-000) shaped
        k.res = clamp01 (c.v (RES) * m.resMul);
        const float drive = clamp01 (c.v (DRIVE) + m.drive);
        k.driveG = drive > 0.001f ? 1.0f + drive * 6.0f : 0.0f;
        k.driveNorm = 1.2f / std::sqrt (std::max (1.0f, k.driveG));
        // Over the first 10 % the drive fades in from exactly clean, so leaving 0 never steps
        // the level (the shaper alone is +1.6 dB at its gentlest).
        k.driveW = std::min (1.0f, drive / 0.1f);
        k.color = c.v (COLOR); k.det = c.v (DETUNE);
        if (m.engine == E_GRAIN)
        {
            // (SF-000) each from its knob. Where they start: 0.05 of the source's pace, 155 ms, four at once, 0.15 of the
            // source, 0.01 either way: GRANULAR as it was with COLOR and DETUNE in the middle.
            const float scan = juce::jlimit (-1.0f, 1.0f, c.v (GRSCAN));
            k.grPos = clamp01 (c.v (GRPOS));
            k.grSpeed = scan * std::abs (scan) * 2.0f;
            k.grLenS = 0.005f * std::pow (100.0f, clamp01 (c.v (GRSIZE)));
            k.grOv = 0.5f * std::pow (16.0f, clamp01 (c.v (GRDENS)));
            { const float sp = clamp01 (c.v (GRSPRAY)); k.grSpray = sp * sp; }
            k.grPitch = clamp01 (c.v (GRPITCH));
            k.grShape = clamp01 (c.v (GRSHAPE));
            k.grRev = clamp01 (c.v (GRREV));
            k.grClock = c.ci (GRCLOCK); k.grSpread = c.ci (GRSPREAD); k.grFollow = c.ci (GRFOLLOW); k.grSync = c.ci (GRSYNC);
        }
        const float fe = v.fenv.v;
        const float keyTrack = std::log2 (std::max (20.0f, v.freq) / 130.0f) * 0.3f;
        k.cutT = clamp01 (cutK + envA * fe * 0.6f + keyTrack * 0.1f) * std::pow (0.75f, (float) v.gen);
        // DRIFT: analog voices are never quite alike, nor still. A poly model's voices each sit off
        // tune (up to 5 cents) and off cutoff (up to a fifth of an octave), the slot's own, as on a
        // poly synth's voice cards; every voice wanders (up to 4 cents, a slow pair of drifts 6 and
        // 14 s long at their own pace; a mono model's one oscillator wanders one way whatever the
        // slot, so its notes join up, and its cutoff wanders with it).
        float driftSt = 0.0f, driftOct = 0.0f;
        if (const float dr = c.v (DRIFT); dr > 0.0f)
        {
            const auto& d = slotDrift[m.mono ? 0 : (size_t) (&v - voices.data())];
            const float t = (float) driftClock * d.rate;
            const float w = 0.6f * std::sin (kTwoPi * (0.071f * t + d.ph)) + 0.4f * std::sin (kTwoPi * (0.17f * t + 2.3f * d.ph));
            driftSt = dr * (0.04f * w + (m.mono ? 0.0f : 0.05f * d.tune));
            driftOct = dr * (m.mono ? 0.1f * w : 0.2f * d.cut);
        }
        const float cutHz = expMap (k.cutT, 35.0f, 18000.0f) * (driftOct != 0.0f ? std::exp2 (driftOct) : 1.0f);
        switch (m.filt)
        {
            case F_LADDER: v.ladder.set (cutHz, k.res * 0.98f, vr); break;
            case F_SVF:    v.svf.set (cutHz, 0.6f + k.res * 8.0f, vr);
                           if (m.engine == E_SAMPLE || m.engine == E_GRAIN) { v.svfS.g = v.svf.g; v.svfS.k = v.svf.k; v.svfS.a1 = v.svf.a1; v.svfS.a2 = v.svf.a2; v.svfS.a3 = v.svf.a3; }     // (SF-000)
                           break;
            case F_MS20:   v.svf.set (cutHz, 0.6f + k.res * 12.0f, vr); v.svf2.set (std::max (20.0f, cutHz * 0.08f), 0.7f + k.res * 3.0f, vr); break;
            case F_NONE: case F_DIODE: break;      // F_DIODE: per sample (render)
        }
        if (m.acid)
        {
            // TB-303. The cutoff (where the resonance rings) as an x0xb0x's filter current puts it,
            // fitted over CUTOFF and ENV MOD with the gate high and low (a1k0n's measurements, the
            // same analog board as the 303's): at rest 103 Hz + e^(5.23 + 1.70 CUTOFF - 0.68 ENV MOD),
            // at the envelope's peak 103 Hz + e^(5.56 + 2.18 CUTOFF + 1.99 ENV MOD). So 290 Hz-1.1 kHz
            // at rest and 360 Hz-2.4 kHz at the peak with no ENV MOD, and at full ENV MOD the rest
            // drops while the peak climbs (200 Hz-16.9 kHz at full CUTOFF): ENV MOD widens the sweep
            // both ways, as the 303's does. The envelope moves the exponent (the converter's base
            // voltage), so the sweep falls in octaves. 0.85: this filter rings 1.18 x above the
            // frequency it is set to at full RESONANCE. DENSITY's ghosts half an octave darker each.
            // The accent sweep adds up to 1-3 octaves (more with RESONANCE) at a full charge.
            // DECAY 0.2-2.5 s (to about 5 %); an accent forces the shortest, whatever DECAY says, as
            // the 303's accent switches its decay pot out. The sweep capacitor charges from the
            // accent's share of the envelope in about 20 ms and drains in 0.15-0.5 s (RESONANCE's
            // second gang), so accents close together climb on each other.
            const float em = clamp01 (c.v (ENVAMT) * m.envMul), dark = 0.3466f * (float) v.gen + 0.1625f;
            k.acidL0 = 5.2262f + 1.7042f * cutK - 0.6838f * em - dark + 0.6931f * driftOct;
            k.acidL1 = 5.5592f + 2.1779f * cutK + 1.9922f * em - dark + 0.6931f * driftOct;
            k.acidRes = clamp01 (c.v (RES) * m.resMul);
            k.acidSweep = 0.6931f * (1.0f + 2.0f * k.acidRes);
            k.acidFbK = 0.96f * std::pow (k.acidRes, 0.7f);
            // RESONANCE's second gang: the 303 makes up the level its feedback takes from the pass
            // band (1 / (1 + k) above the feedback's high-pass). (1 + k)^0.7 holds the loudness of a
            // played line within 1.5 dB from no RESONANCE to full (SFTests --acid prints it) and
            // leaves the pass band 7 dB down at full, as a1k0n's measured x0xb0x does.
            k.acidComp = std::pow (1.0f + 16.3f * std::pow (k.acidRes, 0.7f), 0.7f);
            const float decT = 0.2f * std::pow (12.5f, c.v (DECAY));
            k.megK = std::exp (-3.0f / (decT * (float) vr));
            k.megKAcc = std::exp (-3.0f / (0.2f * (float) vr));
            k.capC = 1.0f - std::exp (-1.0f / (0.02f * (float) vr));
            k.capD = std::exp (-1.0f / (acidDrainSeconds (k.acidRes) * (float) vr));
            k.sqTarget = c.v (COLOR) < 0.5f ? 0.0f : 1.0f;     // the 303's switch: saw or square
            k.sqFade = 1.0f - std::exp (-1.0f / (0.005f * (float) vr));
            k.sqHpK = std::exp (-kTwoPi * 40.0f / (float) vr);
        }
        if (m.engine == E_FM4)
        {
            // FM 4-OP: each operator's ratio, level and envelope (DECAY 0-98 %: falls with a time
            // constant of 0.03-7.5 s, times the DECAY knob's scaling; above: held), the depth a
            // modulator at full LEVEL gives (pi/2 to 2 pi with COLOR, more on an accent), D's
            // feedback, and the carriers scaled by 1 / sqrt of how many there are.
            k.fmAlgo = juce::jlimit (0, 7, c.ci (FMALGO));
            const float decK = std::pow (2.0f, (c.v (DECAY) - 0.5f) * 4.0f);
            int carriers = 0;
            for (int op = 0; op < 4; ++op)
            {
                k.fmRatio[op] = kFmRatios[juce::jlimit (0, (int) std::size (kFmRatios) - 1, c.ci (FMRATIO + op))];
                // (SF-000) DETUNE: the operators a few cents apart, so they beat (not on one voice in the middle: BASS)
                if (m.engine == E_FM4 && ! m.mono && k.det > 0.001f) { static const float cents[4] = { 6.0f, -6.0f, 3.0f, -3.0f }; k.fmRatio[op] *= semis (k.det * cents[op] / 100.0f); }
                k.fmLevel[op] = clamp01 (c.v (FMLEVEL + op));
                const float d = c.v (FMDECAY + op);
                k.fmMul[op] = d >= 0.98f ? 1.0f : std::exp (-1.0f / (0.03f * std::pow (250.0f, d) * decK * (float) vr));
                if (kFmAlgos[k.fmAlgo].carriers & (1 << op)) ++carriers;
            }
            k.fmBeta = kPi * (0.5f + 1.5f * k.color) * (1.0f + 0.5f * v.accent);
            k.fmFb = c.v (FMFB) * 0.75f * kPi;
            k.fmOut = 1.0f / std::sqrt ((float) std::max (1, carriers));
        }
        if (m.engine == E_NOISE) { v.nbp.set (v.freq, 4.0f + k.color * 40.0f, vr); if (k.det > 0.001f) v.nbp2.set (std::min ((float) (v.freq * semis (k.det * 7.0f)), (float) (0.45 * vr)), 4.0f + k.color * 40.0f, vr); }      // (SF-000) DETUNE: a second band, up to a fifth over it
        k.uni = std::max (1, std::min (8, m.unison));
        k.detCents = m.detune * 40.0f * (0.2f + k.det * 1.6f);
        k.gainAll = v.gain * v.vel * (1.0f + v.accent * 0.5f) * 0.32f * m.gain;
        // Pitch multipliers that move slowly.
        k.subMul = 1.0f;
        if (m.pitchDive > 0.0f) k.subMul *= semis (-m.pitchDive * std::exp (-v.t / 0.08f));
        if (m.wow > 0.0f)
        {
            v.drift += 0.3f * (float) n * invOsr; if (v.drift > 1.0f) v.drift -= 1.0f;
            k.subMul *= semis (m.wow * (0.12f * std::sin (kTwoPi * v.drift) + 0.03f * std::sin (kTwoPi * v.drift * 17.0f)));
        }
        if (driftSt != 0.0f) k.subMul *= semis (driftSt);
        // WARP: tape wow (two slow sines, up to 35 cents) and flutter (6.3 Hz, 5 cents).
        if (const float warp = c.v (WARP); warp > 0.0f)
        {
            v.warpT += (float) n * invOsr;
            const float t = v.warpT * v.warpRate;
            const float wow = 0.6f * std::sin (kTwoPi * (0.37f * t + v.warpPh)) + 0.4f * std::sin (kTwoPi * (0.61f * t + 2.3f * v.warpPh));
            const float flutter = std::sin (kTwoPi * (6.3f * t + 3.7f * v.warpPh));
            k.subMul *= semis (warp * (0.35f * wow + 0.05f * flutter));
        }
        const float fTop = std::max (v.freq, v.target) * k.subMul;
        k.shape1 = m.engine == E_HOOVER ? 1 : (m.acid ? (k.color < 0.5f ? 0 : 1) : m.wave);
        for (int u = 0; u < k.uni; ++u)
        {
            const float cents = k.uni == 1 ? 0.0f : k.detCents * ((float) u / (float) (k.uni - 1) - 0.5f) * 2.0f;
            k.uniR[u] = semis (cents / 100.0f);
            k.pw[u] = m.engine == E_HOOVER ? 0.5f + 0.35f * std::sin (kTwoPi * (v.t * 0.7f + (float) u * 0.3f)) : 0.5f;
            k.r2[u] = m.ratio * semis ((k.detCents * 0.5f + k.det * 7.0f * (m.unison > 1 ? 0.0f : 1.0f)) / 100.0f);
            k.oct1[u] = W.saw.octaveFor (fTop * k.uniR[u]);
            k.oct2[u] = W.saw.octaveFor (fTop * k.uniR[u] * k.r2[u]);
        }
        k.octSub = W.square.octaveFor (fTop * 0.5f);
        k.pw2 = 0.3f + k.color * 0.4f;
        k.osc2Amt = m.osc2 * (m.acid ? 0.0f : 1.0f);
        k.doSub = m.sub > 0.0f || (m.engine == E_SUB && ! m.acid && k.color > 0.0f && m.mono);
        k.subLvl = m.sub * (m.mono ? (0.5f + k.color) : 1.0f) * 0.7f;
        k.sideK = clamp01 (k.cutT * 1.5f);
    }

    void render (Ctx& c, Voice& v, float* L, float* R, int n, const SampleData* smp, bool onGrid)
    {
        const auto& m = models[(size_t) v.model];
        const auto& W = *waves;
        const auto& grainRead = GrainRead::get();          // (SF-000)
        const double vr = v.rate;
        const float invOsr = (float) (1.0 / vr);
        if (onGrid || v.ctlStale) { updateControls (c, v, kSub * v.os); v.ctlStale = false; }
        const auto& k = v.ctl;
        const float res = k.res; juce::ignoreUnused (res);
        const float driveG = k.driveG, driveNorm = k.driveNorm, driveW = k.driveW, color = k.color, det = k.det, gainAll = k.gainAll, subMul = k.subMul;
        const double wtDet = m.detune > 0.0f ? (double) semis (k.detCents / 100.0f) : 1.0;      // (SF-000) WAVETABLE's second reading
        // (SF-000) DETUNE where an engine had none: a second 303 or a second sync up to a quarter of a semitone off, the organ's
        // drawbars each a few cents from true. Not on BASS, whose voice stays one and in the middle.
        const bool detOn = det > 0.001f && roleId != ROLE_BASS;
        const double det2 = (double) semis (det * 0.25f);
        const float detW = std::min (1.0f, det * 2.0f), detN = 1.0f / std::sqrt (1.0f + detW * detW);
        double orgR[6] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
        if (m.engine == E_ORGAN && detOn) { static const float cents[6] = { 0.0f, 7.0f, -5.0f, 9.0f, -8.0f, 11.0f }; for (int q = 0; q < 6; ++q) orgR[q] = (double) semis (det * cents[q] / 100.0f); }
        // (SF-000) a sample with KNOBS on: DETUNE's mix and COLOR's tilt, worked out once for the block
        const bool smpKnobs = m.engine == E_SAMPLE && v.shaped;
        const float smpW = std::min (1.0f, det * 4.0f), smpN = 1.0f / std::sqrt (1.0f + smpW * smpW), smpTilt = smpKnobs || m.tilt ? (color - 0.5f) * 2.0f : 0.0f;
        const float tiltA = 1.0f - std::exp (-kTwoPi * 1000.0f / (float) v.rate), tiltLo = std::pow (10.0f, -smpTilt * 0.45f), tiltHi = std::pow (10.0f, smpTilt * 0.45f);
        const int uni = k.uni, shape1 = k.shape1, octSub = k.octSub;
        const float* uniR = k.uniR; const float* pw = k.pw; const float* r2 = k.r2;
        const int* oct1 = k.oct1; const int* oct2 = k.oct2;
        const float pw2 = k.pw2, osc2Amt = k.osc2Amt, subLvl = k.subLvl, sideK = k.sideK, acidComp = k.acidComp;
        const bool doSub = k.doSub;
        const float drive = driveG > 0.0f ? 1.0f : 0.0f;
        auto sinT = [&W] (double ph) { return W.frame[0].read (0, ph); };
        // Band-limited oscillator read: 0 saw, 1 pulse (pw), 2 triangle, 3 sine.
        auto osc = [&W, &sinT] (int shape, int oct, double ph, float pwv) -> float
        {
            switch (shape)
            {
                case 0: return W.saw.read (oct, ph);
                case 1:
                {
                    if (std::abs (pwv - 0.5f) < 1e-3f) return W.square.read (oct, ph);
                    double p2 = ph - pwv; if (p2 < 0.0) p2 += 1.0;
                    return (2.0f * pwv - 1.0f) - (W.saw.read (oct, ph) - W.saw.read (oct, p2));
                }
                case 2: return W.tri.read (oct, ph);
                default: return sinT (ph);
            }
        };

        for (int i = 0; i < n; ++i)
        {
            if (v.glideCoef > 0.0f) v.freq = v.target + v.glideCoef * (v.freq - v.target); else v.freq = v.target;
            const float f = v.freq * subMul;
            const double inc = (double) f * invOsr;
            const float a = v.amp.tick();
            v.fenv.tick();
            float x = 0.0f, xl = 0.0f, xr = 0.0f;
            bool stereo = false;
            float acidFb = 0.0f;
            switch (m.engine)
            {
                case E_SUB:
                case E_HOOVER:
                {
                    if (m.acid)
                    {
                        // TB-303. The saw, or the square the 303's waveshaper makes of it (a transistor's
                        // soft knee on the saw, band-limited: WaveSet::square303), then a coupling
                        // capacitor's droop (40 Hz). The switch fades over 5 ms.
                        float sw = W.saw.read (oct1[0], v.ph[0]);
                        float sqRaw = W.square303.read (oct1[0], v.ph[0]);
                        v.ph[0] += inc; wrap01 (v.ph[0]);
                        if (detOn)
                        {
                            // (SF-000) DETUNE: a second one beside it
                            sw = (sw + detW * W.saw.read (oct1[0], v.ph2[0])) * detN; sqRaw = (sqRaw + detW * W.square303.read (oct1[0], v.ph2[0])) * detN;
                            v.ph2[0] += inc * det2; wrap01 (v.ph2[0]);
                        }
                        v.sqHp = k.sqHpK * (v.sqHp + sqRaw - v.sqPrev); v.sqPrev = sqRaw;
                        v.sqMix += (k.sqTarget - v.sqMix) * k.sqFade;
                        x = 0.6f * ((1.0f - v.sqMix) * sw + v.sqMix * 0.68f * v.sqHp);
                        xl = xr = x;
                        // The envelope, and the accent sweep's capacitor (one per layer, so it outlives a note).
                        v.meg *= v.accent > 0.0f ? k.megKAcc : k.megK;
                        float& cap = acidCap[(size_t) (v.layer & 1)];
                        if (! v.releasing)
                        {
                            const float in = v.accent * v.meg;
                            cap = in > cap ? cap + (in - cap) * k.capC : cap * k.capD;
                            acidOwned[(size_t) (v.layer & 1)] = true;
                        }
                        const float hz = std::min (16000.0f, 87.5f + std::exp (k.acidL0 + (k.acidL1 - k.acidL0) * v.meg + k.acidSweep * cap));
                        v.dl.set (hz, vr);
                        acidFb = k.acidFbK * DiodeLadder303::oscillatesAt (hz);
                        break;
                    }
                    for (int u = 0; u < uni; ++u)
                    {
                        float y = osc (shape1, oct1[u], v.ph[u], pw[u]);
                        v.ph[u] += inc * uniR[u]; wrap01 (v.ph[u]);
                        if (osc2Amt > 0.0f && m.engine != E_HOOVER)
                        {
                            y += osc (m.wave2, oct2[u], v.ph2[u], pw2) * osc2Amt;
                            v.ph2[u] += inc * uniR[u] * r2[u]; wrap01 (v.ph2[u]);
                        }
                        if (uni > 1) { xl += y * (1.0f - v.pan[u]); xr += y * v.pan[u]; stereo = true; }
                        else x += y;
                    }
                    if (uni > 1) { const float k = 1.0f / std::sqrt ((float) uni); xl *= k; xr *= k; }
                    if (doSub)
                    {
                        const float sq = W.square.read (octSub, v.subPh) * subLvl;
                        v.subPh += inc * 0.5; wrap01 (v.subPh);
                        x += sq; xl += sq; xr += sq;
                    }
                    if (m.noise > 0.0f) { const float nz = rng.next() * m.noise; x += nz; xl += nz; xr += nz; }
                    break;
                }
                case E_ORGAN:
                {
                    static const double h[6] = { 1, 2, 3, 4, 6, 8 };
                    const float w[6] = { 1.0f, 0.8f * color + 0.2f, 0.9f, 0.5f * color, 0.3f, 0.25f * color };
                    v.ph[0] += inc; wrap01 (v.ph[0]);
                    for (int k = 0; k < 6; ++k)
                    {
                        // (SF-000) each drawbar keeps its own place, so that DETUNE can set them a few cents from true
                        v.ph2[k] += inc * h[k] * orgR[k]; v.ph2[k] -= std::floor (v.ph2[k]);
                        if ((double) f * h[k] < 0.45 * vr) x += sinT (v.ph2[k]) * w[k];
                    }
                    x *= 0.35f;
                    if (v.t < 0.004f) x += rng.next() * 0.3f * (1.0f - v.t / 0.004f);       // key click
                    { const double p = v.ph[0] * 3.0; x += sinT (p - std::floor (p)) * 0.4f * std::exp (-v.t / 0.2f); }   // percussion
                    break;
                }
                case E_FM:
                {
                    const float idx = (0.5f + color * 6.0f) * (0.3f + 0.7f * v.fenv.v) * (1.0f + v.accent);
                    v.modPh += inc * m.ratio * (0.98 + det * 0.04); wrap01 (v.modPh);
                    double mp = v.modPh + v.fb * (0.3f / kTwoPi); mp -= std::floor (mp);
                    const float mod = sinT (mp);
                    v.fb = mod;
                    v.ph[0] += inc; wrap01 (v.ph[0]);
                    double cp = v.ph[0] + idx * mod / kTwoPi; cp -= std::floor (cp);
                    x = sinT (cp);
                    break;
                }
                case E_FM4:
                {
                    // The operators D, C, B, A (modulation only runs down the letters); D's
                    // feedback is its last two outputs averaged (as the DX's, so it never rings).
                    const auto& al = kFmAlgos[k.fmAlgo];
                    float o[4] {}, out = 0.0f;
                    for (int op = 3; op >= 0; --op)
                    {
                        float pm = 0.0f;
                        for (int j = op + 1; j < 4; ++j) if (al.mods[op] & (1 << j)) pm += o[j];
                        if (op == 3) pm += k.fmFb * 0.5f * (v.fmPrev[0] + v.fmPrev[1]);
                        v.fmPh[op] += inc * k.fmRatio[op]; wrap01 (v.fmPh[op]);
                        double pp = v.fmPh[op] + pm * (1.0 / kTwoPi); pp -= std::floor (pp);
                        v.fmEnv[op] *= k.fmMul[op];
                        const float y = sinT (pp) * k.fmLevel[op] * v.fmEnv[op];
                        if (op == 3) { v.fmPrev[1] = v.fmPrev[0]; v.fmPrev[0] = y; }
                        if (al.carriers & (1 << op)) out += y;
                        o[op] = y * k.fmBeta;
                    }
                    x = out * k.fmOut;
                    break;
                }
                case E_SYNC:
                {
                    // Hard sync: slave saw restarts with the master; every edge BLEP-corrected.
                    const float ratio = 1.0f + (color * 4.0f + v.fenv.v * c.v (ENVAMT) * 6.0f);
                    x = v.sync.tick (std::min (0.45, inc), std::min (0.45, inc * ratio)) * 0.8f;
                    if (detOn) x = (x + detW * 0.8f * v.sync2.tick (std::min (0.45, inc * det2), std::min (0.45, inc * det2 * ratio))) * detN;       // (SF-000) DETUNE: a second beside it
                    break;
                }
                case E_NOISE:
                {
                    const float nz = rng.next();
                    float band = v.nbp.tickBP (nz);
                    if (det > 0.001f) band = (band + detW * v.nbp2.tickBP (nz)) * detN;      // (SF-000) DETUNE: the second band
                    x = band * (2.0f + color * 6.0f) + rng.next() * 0.05f * (1.0f - color);
                }
                    break;
                case E_WAVETABLE:
                {
                    // TABLE SAMPLE with a sample's frames: COLOR across its eight; else the built-in four.
                    const float scan = clamp01 (color + 0.15f * std::sin (kTwoPi * v.t * 0.13f));
                    auto wave = [&] (double ph)
                    {
                        if (tableFrames != nullptr)
                        {
                            const float pos = scan * (float) (kSampleFrames - 1);
                            const int t0 = std::min (kSampleFrames - 2, (int) pos);
                            const float fr = pos - (float) t0;
                            return lerp (tableFrames->f[(size_t) t0].read (oct1[0], ph), tableFrames->f[(size_t) t0 + 1].read (oct1[0], ph), fr);
                        }
                        const float pos = scan * 3.0f;
                        const int t0 = std::min (2, (int) pos);
                        const float fr = pos - (float) t0;
                        return lerp (W.frame[t0].read (oct1[0], ph), W.frame[t0 + 1].read (oct1[0], ph), fr);
                    };
                    x = wave (v.ph[0]);
                    v.ph[0] += inc; wrap01 (v.ph[0]);
                    if (m.detune > 0.0f)
                    {
                        // (SF-000) DETUNE: a second reading of the same waves a few cents off (the model's detune, by the knob)
                        x = (x + wave (v.ph[1])) * 0.7071f;
                        v.ph[1] += inc * wtDet; wrap01 (v.ph[1]);
                    }
                    break;
                }
                case E_GRAIN:
                {
                    const auto& src = smp != nullptr ? smp->buf : grainSrc;
                    const auto& mips = smp != nullptr ? smp->mips : grainMips;
                    // A loaded sample's grains start inside START..END as the knobs are now (moving
                    // them moves where a held note's grains come from); each plays out in the part
                    // it started in, faded at its ends, so a part moved while grains play never cuts
                    // one. The built-in source is played whole. A part shorter than the grains makes
                    // them shorter.
                    // (SF-000) The place they come from is POSITION, carried on by SCAN as the note is held (or, on SYNC,
                    // where the song is in the beats the source spans); SPRAY strays from it. See the knobs' own words.
                    const int srcLen = src.getNumSamples();
                    const bool inReg = smp != nullptr && blockReg.end > 0.0;
                    const int r0 = inReg ? (int) blockReg.start : 0;
                    const int len = (inReg ? std::min (srcLen, blockReg.last()) : srcLen) - r0;
                    const float root = smp != nullptr && smp->rootHz > 0 ? smp->rootHz : 130.81f;
                    int glen = (int) (k.grLenS * sr);                            // source samples
                    if (inReg && (blockReg.start > 0.0 || blockReg.end < (double) srcLen)) glen = std::min (glen, std::max (16, len - 8));
                    const double beatStep = c.bpm / (60.0 * vr);
                    bool fire = false;
                    if (k.grClock == 0) { if (--v.grainClock <= 0) { fire = true; v.grainClock = std::max (1, (int) ((float) (glen * v.os) / k.grOv)); } }
                    else
                    {
                        // a grain a tick of the song's grid, GRAINS how likely each is (three quarters of the way up: every one)
                        static const double ticks[] = { 1.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
                        const long long tick = (long long) std::floor (v.grBeat / ticks[juce::jlimit (0, 6, k.grClock)]);
                        if (tick != v.grTick) { v.grTick = tick; fire = rng.uni() < (k.grOv <= 0.5f ? 0.0f : std::log (k.grOv * 2.0f) / std::log (8.0f)); }
                    }
                    if (fire && len > glen + 4)
                    {
                        for (auto& gr : v.grains)
                            if (! gr.on)
                            {
                                gr.on = true; gr.age = 0; gr.len = glen * v.os;
                                const double span = (double) (len - glen - 4);
                                double at = (double) k.grPos * span * 0.98;         // (not quite all the way round: the top of the knob is the source's end, not its start again)
                                if (k.grSync > 0)
                                {
                                    static const double spans[] = { 1.0, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
                                    const double beats = spans[juce::jlimit (0, 6, k.grSync)];
                                    at += (v.grBeat / beats - std::floor (v.grBeat / beats)) * span;
                                }
                                else at += v.grScan;
                                gr.pos = std::fmod (at + (double) (rng.uni() * k.grSpray * (float) len), span);
                                if (gr.pos < 0) gr.pos += span;
                                gr.pos += r0;
                                gr.reg = inReg ? blockReg : SampleRegion();
                                // its pitch: the note's (or the source's own), and as far from it as PITCH lets it stray
                                double pitch = (k.grFollow == 1 ? (double) semis (pitchOffset) : (double) (f / root)) / v.os;
                                if (k.grSpread == 0) pitch *= 1.0 + (double) ((rng.uni() - 0.5f) * k.grPitch * 0.04f);
                                else if (rng.uni() < k.grPitch)
                                {
                                    static const double oct[] = { 0.5, 2.0 }, fifths[] = { 0.5, 0.75, 1.5, 2.0 };
                                    const float pick = rng.uni();
                                    pitch *= k.grSpread == 1 ? oct[pick < 0.5f ? 0 : 1] : fifths[juce::jlimit (0, 3, (int) (pick * 4.0f))];
                                }
                                gr.rate = pitch;
                                gr.pan = 0.5f + (rng.uni() - 0.5f) * 0.8f;
                                // (SF-000) Settled once, as the grain starts. A grain played above the source's pitch reads the
                                // octave-down copy on which it advances a sample a step at the most (GrainRead: it was the copy
                                // an octave above that one, through sampleAt's low-pass). The copy holds the source to 0.86 of
                                // its own band; the voice runs at twice the rate, so that is 20.6 kHz at the least. One at or
                                // below the source's pitch reads the source as it did. Its window turns a step a sample (four
                                // multiplies) instead of a cosine a sample.
                                gr.lv = -1; gr.sc = 1.0;
                                for (double r = gr.rate; r > 1.0 && gr.lv + 1 < (int) mips.lv.size(); r *= 0.5) { ++gr.lv; gr.sc *= 0.5; }
                                { const double turn = (double) kTwoPi / (double) gr.len; gr.wc = 1.0; gr.ws = 0.0; gr.rc = std::cos (turn); gr.rs = std::sin (turn); }
                                // backwards: from where it would have ended (as much of the source as its length takes at its pitch)
                                if (k.grRev > 0.0f && rng.uni() < k.grRev)
                                {
                                    const double end = gr.pos + gr.rate * (double) gr.len, last = (double) (inReg ? std::min (srcLen, blockReg.last()) : srcLen) - 3.0;
                                    gr.pos = std::min (end, last); gr.rate = -gr.rate;
                                }
                                break;
                            }
                    }
                    if (k.grSync == 0) { v.grScan += (double) k.grSpeed / v.os; const double span = (double) std::max (1, len - glen - 4); if (v.grScan >= span) v.grScan -= span; else if (v.grScan < 0.0) v.grScan += span; }
                    v.grBeat += beatStep;
                    stereo = true;
                    // (SF-000) SHAPE: the rounded window in the middle (as it always was), toward a struck one at the left
                    // and a flat-topped one at the right
                    const float shapeA = k.grShape < 0.5f ? (0.5f - k.grShape) * 2.0f : 0.0f, shapeB = k.grShape > 0.5f ? (k.grShape - 0.5f) * 2.0f : 0.0f;
                    for (auto& gr : v.grains)
                    {
                        if (! gr.on) continue;
                        const bool own = gr.reg.end > 0.0;
                        const double apos = gr.rate < 0.0 ? -gr.rate : gr.rate;
                        if ((int) gr.pos + 2 >= (own ? std::min (srcLen, gr.reg.last()) : srcLen) || gr.pos < 1.0 || gr.age >= gr.len) { gr.on = false; continue; }
                        float w = (float) (0.5 - 0.5 * gr.wc);
                        { const double c2 = gr.wc * gr.rc - gr.ws * gr.rs; gr.ws = gr.ws * gr.rc + gr.wc * gr.rs; gr.wc = c2; }
                        if (shapeA > 0.0f || shapeB > 0.0f)
                        {
                            const float ph = (float) gr.age / (float) gr.len, back = 1.0f - ph;
                            if (shapeA > 0.0f) w += (std::min (1.0f, ph * 50.0f) * back * back * back - w) * shapeA;
                            else               w += (std::min (1.0f, std::min (ph, back) * 10.0f) - w) * shapeB;
                        }
                        // (its copy, if the source still has one: another sample given while it plays may have fewer)
                        const auto& from = gr.lv >= 0 && gr.lv < (int) mips.lv.size() ? mips.lv[(size_t) gr.lv] : src;
                        const double sc = &from == &src ? 1.0 : gr.sc, at = gr.pos * sc, step = apos * sc;
                        const int fromLen = from.getNumSamples();
                        const float* c0 = from.getReadPointer (0); const float* c1 = from.getReadPointer (std::min (1, from.getNumChannels() - 1));
                        // (a copy, at a sample a step or less: the table. The source itself, or a copy still read faster than
                        // that because the source has no deeper one: as before)
                        // (SF-000) the two channels apart: the left of the recording to the left, its right to the right, and the
                        // grain's own place across them as before (a file of one channel: as it always was)
                        float gl, gR;
                        if (&from != &src && step <= 1.0) grainRead.both (c0, c1, fromLen, at, gl, gR);
                        else { gl = sampleAt (c0, fromLen, at, step); gR = c1 == c0 ? gl : sampleAt (c1, fromLen, at, step); }
                        const float gg = own ? gr.reg.gain (gr.pos) : 1.0f;
                        xl += gl * w * gg * (1.0f - gr.pan); xr += gR * w * gg * gr.pan;
                        gr.pos += gr.rate; ++gr.age;
                    }
                    break;
                }
                case E_SAMPLE:
                {
                    if (smp == nullptr) { v.active = false; return; }
                    const int len = v.reg.end > 0.0 ? std::min (smp->length(), v.reg.last()) : smp->length();
                    if ((int) v.spos + 2 >= len) { v.active = false; return; }
                    const double rate = (double) f / (double) (v.sliceHz > 0.0f ? v.sliceHz : noteHz (smp->rootNote)) / v.os;      // (SF-000) a slice: against its own note
                    const float rg = v.reg.gain (v.spos);
                    xl = smp->read (0, v.spos, rate) * rg;
                    xr = smp->read (1, v.spos, rate) * rg;
                    stereo = true;
                    // (SF-000) LOOP: over the last loopX of what plays, its end fades out as its start (from as far before
                    // START + loopX) fades in, at equal power; then the place is back at START + loopX, and on it goes
                    if (v.loopX > 0.0)
                    {
                        const double end = std::min ((double) len - 3.0, v.reg.end), turn = end - v.reg.start - v.loopX;
                        if (turn > 32.0 && v.spos >= end - v.loopX)
                        {
                            const float t = clamp01 ((float) ((v.spos - (end - v.loopX)) / v.loopX));
                            const float a = std::cos (0.5f * kPi * t), b = std::sin (0.5f * kPi * t) * v.reg.level;
                            const double from = v.spos - turn;
                            xl = xl * a + smp->read (0, from, rate) * b;
                            xr = xr * a + smp->read (1, from, rate) * b;
                            if (v.spos + rate >= end) { v.spos -= turn; v.spos2 -= turn; v.looped = true; v.reg.fadeIn = 0.0; }
                        }
                    }
                    v.spos += rate;
                    if (v.shaped)
                    {
                        // (SF-000) DETUNE: a second reading up to 30 cents off, from the same place (not on a mono model)
                        if (det > 0.001f && ! m.mono)
                        {
                            if (v.spos2 < v.reg.start) v.spos2 = v.spos - rate;
                            if ((int) v.spos2 + 2 < len)
                            {
                                const float rg2 = v.reg.gain (v.spos2) * smpW;
                                xl = (xl + smp->read (0, v.spos2, rate) * rg2) * smpN;
                                xr = (xr + smp->read (1, v.spos2, rate) * rg2) * smpN;
                            }
                            v.spos2 += rate * (1.0 + (double) det * 0.0175);
                        }
                        else v.spos2 = v.spos;
                        // THROUGH: GATE is how much of it plays
                        if (v.cutPos > 0.0 && v.spos >= v.cutPos && ! v.releasing) { v.amp.off(); v.fenv.off(); v.releasing = true; }
                    }
                    break;
                }
            }
            if (! stereo) { xl = x; xr = x; }
            // (SF-000) COLOR as a tilt about 1 kHz, 9 dB each way at its ends and none at the centre: a sample with KNOBS on, and
            // the models that read no COLOR of their own
            if (std::abs (smpTilt) > 0.002f)
            {
                v.tilt[0] += tiltA * (xl - v.tilt[0]); v.tilt[1] += tiltA * (xr - v.tilt[1]);
                xl = v.tilt[0] * tiltLo + (xl - v.tilt[0]) * tiltHi;
                xr = v.tilt[1] * tiltLo + (xr - v.tilt[1]) * tiltHi;
            }
            // Filter (mono filters run on the mid; stereo engines get mid/side filtered as one).
            auto filt = [&] (float in) -> float
            {
                if (drive > 0.0f)
                {
                    const float xin = in * driveG;
                    if (! v.driveOn) { v.driveSat.prime (xin); v.driveOn = true; }     // no stale ADAA history
                    in += driveW * (v.driveSat.tick (xin) * driveNorm - in);
                }
                else v.driveOn = false;
                switch (m.filt)
                {
                    case F_DIODE: return v.dl.tick (in, acidFb) * acidComp;
                    case F_LADDER: return v.ladder.tick (in);
                    case F_SVF: return v.svf.tickLP (in);
                    case F_MS20: return v.ms20Sat.tick (v.svf2.tickHP (v.svf.tickLP (in)) * 1.5f);
                    default: return in;
                }
            };
            float yl, yr;
            if (stereo && m.filt != F_NONE)
            {
                const float mid = filt (0.5f * (xl + xr));
                const float side = 0.5f * (xl - xr);
                // (SF-000) a file's own two sides (SAMPLE, GRANULAR): what is between them goes through the filter too, so
                // a stereo recording is filtered as one. (The other engines' spread: turned down as the filter closes, as before.)
                if (m.filt == F_SVF && (m.engine == E_SAMPLE || m.engine == E_GRAIN)) { const float s2 = side != 0.0f || v.svfS.ic1 != 0.0f || v.svfS.ic2 != 0.0f ? v.svfS.tickLP (side) : 0.0f; yl = mid + s2; yr = mid - s2; }
                else { yl = mid + side * sideK; yr = mid - side * sideK; }
            }
            else if (stereo) { yl = xl; yr = xr; }
            else { yl = yr = filt (xl); }
            L[i] += yl * a * gainAll;
            R[i] += yr * a * gainAll;
            v.t += invOsr;
            if (v.autoOff > 0 && --v.autoOff == 0 && ! v.releasing && ! v.through) { v.amp.off(); v.fenv.off(); v.releasing = true; }
            if (v.amp.st == Adsr::Idle && v.t > 0.001f) { v.active = false; return; }
        }
    }

    void buildGrainSource()
    {
        // Default grain source: 2 s of a slowly beating minor chord through a soft low-pass.
        const int n = (int) (sr * 2.0);
        grainSrc.setSize (2, n);
        Svf lp; lp.set (2500.0f, 0.7f, sr);
        double p[4] {};
        const float fr[4] = { 130.81f, 155.56f, 196.0f, 261.63f };
        for (int i = 0; i < n; ++i)
        {
            float x = 0;
            for (int k = 0; k < 4; ++k) { p[k] += fr[k] * (1.0 + 0.002 * std::sin (i * 0.0001 * (k + 1))) / sr; if (p[k] > 1) p[k] -= 1; x += (float) (2.0 * p[k] - 1.0); }
            x = lp.tickLP (x * 0.2f);
            grainSrc.setSample (0, i, x); grainSrc.setSample (1, i, x);
        }
        grainMips.build (grainSrc);
        GrainRead::get();          // (SF-000) the grains' read table, made here and not on the audio thread
    }

    std::string name;
    int roleId;
    std::vector<SynthModel> models;
    float dCut, dRes, dEnv, dGate, dPocket;
    double sr = 48000.0;
    // Playing has kPlayVoices; the pool is twice that, so a held chord of 16 voices plays on through a
    // MODEL change while the 16 it replaces release (allocVoice).
    static constexpr int kPlayVoices = 16;
    std::array<Voice, 2 * kPlayVoices> voices;
    bool restarting = false;                           // a MODEL change is playing the keys down again
    std::vector<int> held;
    struct Down { int note; float vel; };
    std::array<Down, 128> down {};                     // the keys down, oldest first (keyDown / keyUp)
    int numDown = 0;
    uint64_t renderTick = 1;                           // counts rendered sub-blocks (allocVoice)
    // ACID 303-TYPE's accent sweep capacitor, per layer (the main model's, the layer's), and whether a
    // note of its charged or held it in this sub-block.
    float acidCap[2] {};
    bool acidOwned[2] {};
    float pushed[kParams] {}, pushed0[kParams] {};    // the knobs as ACID and WARP push them (process)
    double segPpq = 0.0;                              // (SF-000) the song's place where the part of the block being rendered starts
    static float acidDrainSeconds (float res) { return 0.15f + 0.35f * res; }
    EchoScheduler echoes;
    std::vector<NoteEvent> events;
    RoleOutput out;
    Ensemble ens;
    SampleSlot sample;
    Noise rng;
    Noise warpRng { 0x2545F491u };
    Noise smpRng { 0x7F4A7C15u };      // (SF-000) SAMPLE's RANDOM START: its own, so nothing else's chances move
    float ensembleAmt = 0.0f, pitchOffset = 0.0f;
    int lastModel = -1;
    double osr = 96000.0;
    std::shared_ptr<const WaveSet> waves;
    Decimator2x dec;
    Halfband dec4[2];
    static constexpr int kSub = 32;
    float L2[2 * kSub] {}, R2[2 * kSub] {}, L4[4 * kSub] {}, R4[4 * kSub] {};
    // LAYER SPLIT: the layer's own sums (2x, 4x, base rate) and halfbands; bLive counts the
    // sub-blocks its halfbands still run (kBDrain after its last voice); the split itself.
    Decimator2x decB;
    Halfband dec4b[2];
    float Lb2[2 * kSub] {}, Rb2[2 * kSub] {}, Lb4[4 * kSub] {}, Rb4[4 * kSub] {}, Lb[kSub] {}, Rb[kSub] {};
    static constexpr int kBDrain = 64;
    int bLive = 0;
    LayerSplit split;
    SharedSlot<SampleFrames> frames;                   // WAVETABLE's frames from the sample (buildFrames)
    const SampleFrames* tableFrames = nullptr;         // this block's (processVoices)
    // DRIFT: each voice slot's tuning and cutoff (-1..1), its drift's phase and pace; the drift's clock (s).
    struct SlotDrift { float tune = 0, cut = 0, ph = 0, rate = 1; };
    std::array<SlotDrift, 2 * kPlayVoices> slotDrift {};
    double driftClock = 0.0;
    juce::AudioBuffer<float> grainSrc;
    Mips grainMips;
    std::atomic<int> activeVoices { 0 };
    SampleRegion blockReg;                             // the sample's START..END in this block (process)
    static constexpr int kOwn = 80;                    // the display's VOICES and pocket
    SampleStrip strip;
};

// Gain staging (Calibration.h): every model at its role's target loudness.
template <size_t N>
static std::vector<SynthModel> calibrated (std::vector<SynthModel> m, const float (&db)[N])
{
    for (size_t i = 0; i < m.size() && i < N; ++i) m[i].gain *= cal::gain (db[i]);
    return m;
}
ModulePtr makeBASS() { return std::make_unique<SynthModule> ("BASS", ROLE_BASS, calibrated (bassModels(), cal::bass), 0.35f, 0.3f, 0.4f, 0.5f, 0.6f); }
ModulePtr makeLEAD() { return std::make_unique<SynthModule> ("LEAD", ROLE_LEAD, calibrated (leadModels(), cal::lead), 0.4f, 0.5f, 0.5f, 0.5f, 0.2f); }
ModulePtr makePAD()  { return std::make_unique<SynthModule> ("PAD", ROLE_PAD, calibrated (padModels(), cal::pad), 0.55f, 0.1f, 0.1f, 0.75f, 0.3f); }
} // namespace sf
