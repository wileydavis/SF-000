#pragma once
// POCKETS: the master's tool for who has which part of the spectrum.
//
// Every lane has a pocket: a stretch of the spectrum that is its own (HOME, WIDTH), held so firmly
// (HOLD). Where two sounds would cover one another, the one with the weaker claim to that stretch
// gives way to the other, there and only for as long as the other is sounding, and by no more than
// it takes to sit CLEAR below it (YIELD: the most a lane gives). The two returns have no pocket:
// echo and reverb give way to every lane.
//
// How: each lane (and each return) is heard through 28 third-octave bands, 31.5 Hz to 16 kHz, and
// carved by 28 bells that only ever cut, in series, with no delay. With AMOUNT at nought nothing
// is looked at and nothing is in the path.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include "Rig.h"

namespace sf0
{
class Pockets
{
public:
    static constexpr int kBands = 28;               // third octaves: 31.25 Hz ... 16 kHz
    static constexpr int kParts = kLanes + 2;       // the lanes, then return A and return B
    static constexpr float kMaxCut = 24.0f;         // dB: what YIELD at full lets a band lose
    static constexpr int kChunk = 32;               // samples between two looks

    // The knobs, each 0 to 1 as the engine has them.
    struct Set
    {
        float amount = 0.0f, clear = 0.0f, speed = 0.0f, returns = 0.0f;
        std::array<float, kLanes> home {}, width {}, hold {}, yield {};
    };
    // What a knob's value means.
    static float bandHz (float band)        { return 31.25f * std::pow (2.0f, band / 3.0f); }      // a place among the bands (0 to 27) as a frequency
    static float homeBand (float v)         { return juce::jlimit (0.0f, 1.0f, v) * (float) (kBands - 1); }
    static float homeHz (float v)           { return bandHz (homeBand (v)); }
    static float homeValue (float hz)       { return juce::jlimit (0.0f, 1.0f, 3.0f * std::log2 (std::max (1.0f, hz) / 31.25f) / (float) (kBands - 1)); }
    static float widthValue (float oct)     { return juce::jlimit (0.0f, 1.0f, std::log (std::max (0.01f, oct) / 0.5f) / std::log (16.0f)); }
    static float widthOct (float v)         { return 0.5f * std::pow (16.0f, juce::jlimit (0.0f, 1.0f, v)); }      // 0.5 to 8 octaves, at half height
    static float clearDb (float v)          { return 18.0f * juce::jlimit (0.0f, 1.0f, v); }
    static float releaseMs (float v)        { return 600.0f * std::pow (25.0f / 600.0f, juce::jlimit (0.0f, 1.0f, v)); }
    // A pocket's height (0 to 1) at a place among the bands: flat over its middle, steep at its edges.
    static float pocketAt (float homeV, float widthV, float band);
    // Who gives way to whom: how far (0 to 1) a sound with claim `mine` on a band gives way to one with claim `theirs`.
    static float gives (float mine, float theirs);
    static float claimOf (float holdV, float pocketHeight)  { return juce::jlimit (0.0f, 1.0f, holdV) * (0.25f + 0.75f * pocketHeight); }

    void prepare (double sampleRate, int maxBlock);
    // The lanes as they leave their strips, two channels each: looked at, and carved in place.
    // watch: look even with AMOUNT at nought (the screen is showing them). Nothing is carved then.
    void lanes (float* const* chans, int n, const Set&, bool watch);
    // The two returns' wet sound (A left, A right, B left, B right), after the lanes of the same
    // buffer: looked at, and carved against what the lanes were holding.
    void returns (float* const* chans, int n);
    bool working() const                    { return live; }       // the last buffer was looked at

    // ---- for the screen and AUTO (message thread)
    struct Seen
    {
        std::array<std::array<float, kBands>, kParts> level {};    // dB: each band of each part as it came, peaks held a moment
        std::array<std::array<float, kBands>, kParts> cut {};      // dB taken out of it (0: none)
        std::array<std::array<float, kBands>, kParts> usual {};    // dB: the band's average over the last seconds it sounded
        std::array<float, kParts> heardFor {};                     // seconds of sound that average is over (to 30)
        bool live = false;
    };
    void seen (Seen&) const;
    // AUTO: where each lane's pocket would be, from what has been playing. Each lane that has been
    // heard for two seconds or more is given a HOME and a WIDTH (placed says which were): the
    // narrowest pocket, of an octave to four, that holds three fifths of the lane's sound, set
    // where it holds the most. HOLD and YIELD are left as they are: who wins where pockets then
    // meet is theirs to say.
    static void place (const Seen&, const Set& now, Set& out, std::array<bool, kLanes>& placed);

    // tests: one band's cut on a part now, in dB
    float cutNow (int part, int band) const { return part >= 0 && part < kParts && band >= 0 && band < kBands ? parts[(size_t) part].gS[band] : 0.0f; }

private:
    struct Part
    {
        // the bank: two band-pass sections a band (their states), and what came through
        alignas (32) float s1[kBands], s2[kBands], s3[kBands], s4[kBands], acc[kBands];
        float e[kBands];                // each band's power as it came through, smoothed a little
        float pw[kBands];               // and with what its neighbours let through taken out: the band's own
        float lev[kBands];              // dB
        float hold[kBands];             // dB: its peak, let down slowly
        float eff[kBands];              // dB: its level as taken (a hit is known by its spectrum before the low bands have rung up)
        float claim[kBands];            // how far it is sounding there now, and matters there (0 to 1)
        float avg[kBands];              // long average power while it sounds
        float shown[kBands], cutShown[kBands];
        float heardFor = 0.0f;
        float bbNow = 0.0f;             // the last chunk's power, all of it
        float bbEnv = 0.0f, bbSlow = 0.0f, bbHold = -120.0f, sinceOnset = 10.0f, top = -120.0f;
        bool armed = true, quiet = true;
        // the carve
        float g[kBands];                // dB wanted
        float gS[kBands];               // dB now
        float m1[kBands];               // each bell's mix as last set
        float c1[kBands], c2[kBands], c3[kBands], mTo[kBands], gSet[kBands];     // and its coefficients, for the cut they were worked out at
        float ic1[2][kBands], ic2[2][kBands];
        bool on[kBands];
        int active = 0;
        int run[kBands], runs = 0;      // the bells to run over the chunk in hand, lowest first
    };
    void clearPart (Part&);
    void look (Part&, const float* l, const float* r, int len);        // one chunk through the bank
    void level (Part&, int len);                                       // its levels, peaks and claims
    void aim (Part&, int len, const float* want);                      // where each of its bells is to be by the end of the chunk
    void cut (int first, int count, float* const* chans, int at, int len);     // the bells of those parts, run over the chunk
    void tables (const Set&);
    void publish();

    double sr = 48000.0;
    int maxChunks = 0;
    bool live = false, carving = false;
    Set set;
    bool setKnown = false;
    float amount = 0.0f, clear = 0.0f, relMs = 120.0f, retMax = 0.0f;
    // the bank's coefficients (the same for every part)
    alignas (32) float a1[kBands], a2[kBands], a3[kBands], kk[kBands];
    // the bells'
    float tb[kBands];                       // tan (pi f / rate)
    float attC[kBands], phi[3] = { 0.0f, 0.0f, 0.0f };
    float leak[5] = {};                     // how much of a band's power the bank shows one to five bands away
    float predictFor[kBands];               // seconds after a hit that a band's level is taken from the hit's known spectrum
    std::array<Part, kParts> parts;
    // who gives way to whom
    alignas (32) float pocket[kLanes][kBands], claimW[kLanes][kBands], giveW[kLanes][kLanes][kBands], retW[kLanes][kBands];
    float maxCut[kLanes] = {};
    // what the returns give way to, chunk by chunk of the buffer in hand
    std::vector<float> topC, topCeil;
    float scratch[2][kChunk] = {};          // (somewhere for a bell with no partner to write)
    mutable juce::SpinLock seenLock;
    Seen latest;
};
} // namespace sf0
