#pragma once
// MOTION: two modulators in every SF plugin (system parameters M1/M2 SOURCE, RATE, TARGET, DEPTH).
// Every source is locked to the song position, so all plugins move together and a set replays
// the same way every time (RANDOM and DRIFT are seeded per plugin instance and saved with it).
//   SINE TRIANGLE SQUARE RANDOM DRIFT  bipolar, centred on the knob
//   RAMP UP / RAMP DOWN                unipolar, from the knob upward (DEPTH > 0) over RATE
//   KICK                               SF KICK reports; RATE sets the release
//   INPUT (effects) / NOTES (instr.)   input level / note velocity; RATE sets the release
//   STEPS [Nov 10]                     bipolar: eight seeded steps of RATE each, looping (a pattern)
//   ENVELOPE [Nov 10]                  unipolar: up in 2 % of RATE, falling over the rest, every RATE
#include "Bus.h"
#include "Module.h"
#include <cmath>
#include <string>
#include <vector>

namespace sf
{
// STEPS and ENVELOPE are appended: a saved source keeps its number.
enum MotionSrc { MOT_OFF = 0, MOT_SINE, MOT_TRI, MOT_SQUARE, MOT_RAMP_UP, MOT_RAMP_DOWN, MOT_RANDOM, MOT_DRIFT, MOT_KICK, MOT_INPUT,
                 MOT_STEPS, MOT_ENV, kNumMotionSrc };

inline std::vector<std::string> motionSourceNames (bool instrument)
{
    return { "OFF", "SINE", "TRIANGLE", "SQUARE", "RAMP UP", "RAMP DOWN", "RANDOM", "DRIFT", "KICK", instrument ? "NOTES" : "INPUT", "STEPS", "ENVELOPE" };
}
// Rates 0-10 are straight; 11-20 (appended, so saved rates keep their place) run against the bar:
// 3/16 5/16 7/16 cycle across the beat, 1/4D 1/2D (dotted) and 1/4T 1/2T (triplet) across the bar,
// 3 5 7 BAR across a 4/8/16-bar phrase. Every one is locked to the song position: 5/16 meets the
// bar line again every 5 bars, 3 BAR every 3 bars (12 in 4/4 for a 4-bar phrase).
inline std::vector<std::string> motionRateNames()
{
    return { "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR", "32 BAR", "64 BAR",
             "3/16", "5/16", "7/16", "1/4D", "1/2D", "1/4T", "1/2T", "3 BAR", "5 BAR", "7 BAR" };
}
inline constexpr int kMotionRates = 21;
inline double motionRateBeats (int i, double beatsPerBar)
{
    static const double q[] = { 0.25, 0.5, 1.0, 2.0 };
    static const double bars[] = { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
    static const double odd[] = { 0.75, 1.25, 1.75, 1.5, 3.0, 2.0 / 3.0, 4.0 / 3.0 };   // 3/16 .. 1/2T, in beats
    static const double oddBars[] = { 3.0, 5.0, 7.0 };
    i = std::max (0, std::min (kMotionRates - 1, i));
    if (i < 4) return q[i];
    if (i < 11) return bars[i - 4] * beatsPerBar;
    if (i < 18) return odd[i - 11];
    return oddBars[i - 18] * beatsPerBar;
}
inline bool motionBipolar (int src) { return src == MOT_SINE || src == MOT_TRI || src == MOT_SQUARE || src == MOT_RANDOM || src == MOT_DRIFT || src == MOT_STEPS; }
inline constexpr int kMotionSteps = 8;

class Motion
{
public:
    void prepare (double rate, uint32_t s) { sr = rate; seed = s; reset(); }
    void reset() { out = 0.0f; env = 0.0f; primed = false; }
    void setSeed (uint32_t s) { seed = s; }

    // Value at ppqEnd (the end of the chunk being prepared). n = chunk length in samples.
    // input01: chunk input level 0..1 (INPUT), or the loudest note-on velocity in the chunk (NOTES).
    float tick (int src, int rateIdx, const Ctx& c, double ppqEnd, int n, float input01)
    {
        const double period = motionRateBeats (rateIdx, c.beatsPerBar);
        const double secPerBeat = 60.0 / c.bpm;
        float target = 0.0f;
        switch (src)
        {
            case MOT_SINE:      target = std::sin (6.28318530718f * (float) phase (ppqEnd, period)); break;
            case MOT_TRI:
            {
                const float p = (float) phase (ppqEnd, period);
                target = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
                break;
            }
            case MOT_SQUARE:    target = phase (ppqEnd, period) < 0.5 ? 1.0f : -1.0f; break;
            case MOT_RAMP_UP:   target = (float) phase (ppqEnd, period); break;
            case MOT_RAMP_DOWN: target = 1.0f - (float) phase (ppqEnd, period); break;
            case MOT_RANDOM:    target = 2.0f * hash01 (step (ppqEnd, period)) - 1.0f; break;
            // STEPS: RANDOM's values, but eight of them, over and over: a pattern that repeats every
            // eight RATEs (its own values, apart from RANDOM's: -1 - the step).
            case MOT_STEPS:
            {
                const int64_t k = step (ppqEnd, period);
                target = 2.0f * hash01 (-1 - (k - kMotionSteps * (int64_t) std::floor ((double) k / kMotionSteps))) - 1.0f;
                break;
            }
            // ENVELOPE: up in 2 % of RATE, then falling (e^-t, 22 % of RATE: 1 % left at the end), every RATE.
            case MOT_ENV:
            {
                const float p = (float) phase (ppqEnd, period);
                target = p < 0.02f ? p / 0.02f : std::exp (-(p - 0.02f) / 0.22f);
                break;
            }
            case MOT_DRIFT:
            {
                // Catmull-Rom through seeded points, one per period: smooth, never repeats.
                const double x = ppqEnd / period;
                const int64_t k = (int64_t) std::floor (x);
                const float t = (float) (x - (double) k);
                const float p0 = 2.0f * hash01 (k - 1) - 1.0f, p1 = 2.0f * hash01 (k) - 1.0f;
                const float p2 = 2.0f * hash01 (k + 1) - 1.0f, p3 = 2.0f * hash01 (k + 2) - 1.0f;
                const float y = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                                        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
                target = std::max (-1.0f, std::min (1.0f, y));
                break;
            }
            case MOT_KICK:
            {
                // Envelope of SF KICK hits: instant attack (2 ms), release over RATE.
                const float tau = (float) (period * secPerBeat * 0.25);
                float e = 0.0f;
                if (c.bus != nullptr && c.bus->kickAlive (4000))
                {
                    KickHit hits[kKickRing];
                    const int nh = c.bus->recentKicks (hits, kKickRing);
                    for (int h = 0; h < nh; ++h)
                    {
                        const double dtBeats = ppqEnd - hits[h].ppq;
                        if (dtBeats < 0.0 || dtBeats > 64.0) continue;
                        if (! Bus::kickCurrent (hits[h], dtBeats, secPerBeat, 0.05)) continue;
                        const float dt = (float) (dtBeats * secPerBeat);
                        e = std::max (e, hits[h].vel * std::min (1.0f, dt / 0.002f) * std::exp (-dt / std::max (0.005f, tau)));
                    }
                }
                env = e;
                target = e;
                break;
            }
            case MOT_INPUT:
            {
                const float tau = (float) (period * secPerBeat * 0.25);
                const float rel = std::exp (-(float) n / (float) (std::max (0.005f, tau) * sr));
                const float att = std::exp (-(float) n / (float) (0.005 * sr));
                env = input01 + (input01 > env ? att : rel) * (env - input01);
                target = env;
                break;
            }
            default: target = 0.0f; env = 0.0f; break;
        }
        // 3 ms slew: steps (SQUARE, RANDOM, ramp resets, loop jumps) never click.
        if (! primed || src == MOT_KICK || src == MOT_INPUT) { out = target; primed = true; }
        else
        {
            const float a = std::exp (-(float) n / (float) (0.003 * sr));
            out = target + a * (out - target);
        }
        return out;
    }
    float value() const { return out; }

private:
    static double phase (double ppq, double period)
    {
        const double x = ppq / period;
        return x - std::floor (x);
    }
    static int64_t step (double ppq, double period) { return (int64_t) std::floor (ppq / period + 1e-9); }
    float hash01 (int64_t k) const
    {
        uint64_t x = (uint64_t) k * 0x9E3779B97F4A7C15ull ^ ((uint64_t) seed << 1 | 1u);
        x ^= x >> 33; x *= 0xff51afd7ed558ccdull;
        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull;
        x ^= x >> 33;
        return (float) (x >> 40) * (1.0f / 16777216.0f);
    }
    double sr = 48000.0;
    uint32_t seed = 1;
    float out = 0.0f, env = 0.0f;
    bool primed = false;
};
} // namespace sf
