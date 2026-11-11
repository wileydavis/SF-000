#pragma once
// HEARD BEAT: where the beat lands in what a plugin hears, against the beat of the song position the
// host gives it. For each beat, the steepest rise of the 100 Hz - 1 kHz envelope (a kick's attack;
// sub bass, rumble and pads sit below 100 Hz or rise slowly) between 0.2 beat before the beat and
// 0.4 after it; the onset is where that rise last crossed 30 % of its height. The caller decides
// which beats count (keep); the result comes from the last 8 it kept, and holds while none come (a
// breakdown with no kick).
//
// Why [Nov 7]: in Live a plugin with latency upstream of it (SF BUILD on the LIVE BUS, after every
// source's delay compensation) gets a song position ahead of its audio, and a sequencer clocked by
// Live (the OXI One) plays its notes after Live's beat. On that day's recordings the kick reached
// SF BUILD 80 ms after BUILD's beat, so the drop opened 80 ms before the kick. Run on the groove of
// those recordings, this finds every kick 2.1 ms (+-0.1) after its first click: kBias.
#include "Dsp.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sf
{
class HeardBeat
{
public:
    static constexpr double kFrom = -0.2, kTo = 0.4;   // the window around each beat, in beats
    static constexpr double kRiseSec = 0.020;          // a rise is measured over 20 ms
    static constexpr double kBias = 0.002;             // seconds the onset found lags the kick's first sample
    static constexpr float kMinLevel = 0.001f;         // the rise's top, 100 Hz - 1 kHz (-60 dBFS)
    static constexpr float kFrac = 0.3f;               // the onset: where the rise crosses this much of its height
    static constexpr double kAgree = 0.004;            // beats that agree: within 4 ms of each other's onset
    static constexpr int kKeep = 8, kLock = 4, kMaxCand = 4;

    struct Candidate { double beat, offsetBeats; };   // the beat, and its onset's distance from it

    void prepare (double rate)
    {
        sr = rate;
        dec = std::max (1, (int) std::lround (rate / 12000.0));
        const size_t cap = (size_t) std::ceil (rate / dec * 2.0) + 64;   // 0.6 beat plus run-up, down to 20 BPM
        env.assign (cap, 0.0f); at.assign (cap, 0.0); dq.assign (cap, 0);
        band[0].set (100.0f, 0.7071f, rate); band[1].set (1000.0f, 0.7071f, rate);
        att = (float) std::exp (-1.0 / (0.0005 * rate));
        rel = (float) std::exp (-1.0 / (0.020 * rate));
        band[0].reset(); band[1].reset(); e = 0.0f; phase = 0;
        forget();
    }
    // Everything learned, gone (a new sample rate: the latencies change).
    void forget() { nKept = 0; head = 0; med = 0.0; isLocked = false; restart(); }
    // The window under way, gone (the transport stopped or jumped).
    void restart() { count = 0; started = false; nCand = 0; }

    // One sample (mono) at song position q, beats per sample bps.
    void tick (float x, double q, double bps)
    {
        const float a = std::fabs (band[1].tickLP (band[0].tickHP (x)));
        e = (a > e ? att : rel) * (e - a) + a;
        if (++phase < dec) return;
        phase = 0;
        point (q, bps);
    }

    // The beats whose windows closed since the last call (at most kMaxCand).
    int take (Candidate* out)
    {
        const int n = nCand;
        for (int k = 0; k < n; ++k) out[k] = cand[k];
        nCand = 0;
        return n;
    }
    // Counts one beat: its onset's distance from the beat in seconds (kBias already taken off). The
    // result is the median of the largest group of kept beats that agree within kAgree, whenever it
    // is at least kLock of them and more than half (else the last result holds): onsets that don't
    // keep to the beat (a free-time recording, a part in another meter) scatter and never set it.
    void keep (double sec)
    {
        kept[head] = sec;
        head = (head + 1) % kKeep;
        nKept = std::min (nKept + 1, kKeep);
        if (agreedMedian (kept, nKept, med)) isLocked = true;     // else the last result holds
    }
    // The median of the largest group of v[0..n) (n <= kKeep) that agree within kAgree, if it is at
    // least kLock of them and more than half; false (out untouched) if not.
    static bool agreedMedian (const double* v, int n, double& out)
    {
        int bestN = 0;
        double near[kKeep];
        for (int k = 0; k < n; ++k)
        {
            double g[kKeep];
            int m = 0;
            for (int j = 0; j < n; ++j) if (std::abs (v[j] - v[k]) <= kAgree) g[m++] = v[j];
            if (m > bestN) { bestN = m; std::copy (g, g + m, near); }
        }
        if (bestN < kLock || 2 * bestN <= n) return false;
        std::sort (near, near + bestN);
        out = bestN % 2 ? near[bestN / 2] : 0.5 * (near[bestN / 2 - 1] + near[bestN / 2]);
        return true;
    }
    bool locked() const { return isLocked; }
    double seconds() const { return med; }            // where the beat lands, + = after the host's beat

private:
    // Windows follow one another beat by beat; after a (re)start the first is the first whole one.
    void point (double q, double bps)
    {
        const double runUp = 1.25 * kRiseSec * bps * sr;     // the 20 ms before the window's first rise, and some
        if (started && (q < lastQ - 1e-9 || q > lastQ + 0.25)) restart();
        lastQ = q;
        if (! started) { beat = std::ceil (q - kFrom + runUp); count = 0; started = true; }
        if (q >= beat + kTo) { close(); beat += 1.0; count = 0; }
        if (q >= beat + kFrom - runUp && count < (int) env.size()) { env[(size_t) count] = e; at[(size_t) count] = q; ++count; }
    }
    // The steepest rise inside the window (from the lowest point in the 20 ms before each point: a
    // running minimum), then where it crossed kFrac of its height.
    void close()
    {
        const int L = std::max (2, (int) std::lround (kRiseSec * sr / dec));
        int h = 0, t = 0, best = -1;
        float bestRise = 0.0f;
        for (int j = 0; j < count; ++j)
        {
            while (t > h && env[(size_t) dq[(size_t) (t - 1)]] >= env[(size_t) j]) --t;
            dq[(size_t) t++] = j;
            while (dq[(size_t) h] < j - L) ++h;
            if (j < L || at[(size_t) j] < beat + kFrom || at[(size_t) j] >= beat + kTo) continue;
            const float rise = env[(size_t) j] - env[(size_t) dq[(size_t) h]];
            if (rise > bestRise) { bestRise = rise; best = j; }
        }
        if (best < 0 || nCand >= kMaxCand) return;
        int jm = best - L;
        for (int j = best - L; j <= best; ++j) if (env[(size_t) j] < env[(size_t) jm]) jm = j;
        const float lo = env[(size_t) jm], top = env[(size_t) best];
        if (top < kMinLevel || top < 2.0f * lo) return;
        // Back from the top: the rise that ends there (an earlier bump in the same 20 ms, a hat or the
        // last kick's tail, is not the onset).
        const float th = lo + kFrac * (top - lo);
        int j = best;
        while (j > jm && env[(size_t) (j - 1)] >= th) --j;
        double qOn = at[(size_t) j];
        if (j > jm && env[(size_t) j] > env[(size_t) (j - 1)])
            qOn = at[(size_t) (j - 1)] + (at[(size_t) j] - at[(size_t) (j - 1)]) * (double) ((th - env[(size_t) (j - 1)]) / (env[(size_t) j] - env[(size_t) (j - 1)]));
        cand[nCand++] = { beat, qOn - beat };
    }

    double sr = 48000.0;
    int dec = 4, phase = 0;
    Svf band[2];
    float att = 0.0f, rel = 0.0f, e = 0.0f;
    std::vector<float> env;
    std::vector<double> at;
    std::vector<int> dq;
    int count = 0;
    bool started = false;
    double beat = 0.0, lastQ = 0.0;
    Candidate cand[kMaxCand];
    int nCand = 0;
    double kept[kKeep] {};
    int nKept = 0, head = 0;
    double med = 0.0;
    bool isLocked = false;
};
}
