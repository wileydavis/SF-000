#pragma once
// Metering to broadcast standards, allocation-free (audio thread):
//   TruePeak       8x interpolation (128-tap Kaiser-windowed sinc, 16 taps per phase), max |x|
//                  over the eight phases of every sample: within 0.1 dB of the continuous peak
//                  (BS.1770-4 asks for 4x; 4x can read up to 0.7 dB low on dense material).
//   LoudnessMeter  BS.1770 K-weighting; momentary (400 ms), short-term (3 s) and integrated
//                  loudness (400 ms blocks every 100 ms, -70 LUFS absolute and -10 LU relative
//                  gates, computed from a 0.1 dB histogram: unlimited length, fixed memory).
#include "Dsp.h"
#include <array>

namespace sf
{
class TruePeak
{
public:
    static constexpr int kTaps = 32, kPhases = 8;
    static constexpr int kDelay = kTaps / 2;          // samples between input and the estimate
    TruePeak()
    {
        const double pi = 3.14159265358979323846, beta = 9.0;
        auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 50; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double norm = bessel0 (beta);
        const int N = kTaps * kPhases;
        const double C = N / 2.0;                         // phase 0, tap kTaps/2 = the sample itself
        for (int j = 0; j < N; ++j)
        {
            const double t = (j - C) / kPhases;
            const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double r = (j - C) / C;
            const double w = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / norm;
            h[j % kPhases][j / kPhases] = (float) (sinc * w);
        }
        reset();
    }
    void reset() { for (auto& b : buf) b.fill (0.0f); pos = 0; }
    // Push one stereo sample; returns the true peak (linear) of the sample kDelay ago.
    float push (float l, float r)
    {
        pos = (pos == 0 ? kTaps : pos) - 1;
        buf[0][(size_t) pos] = buf[0][(size_t) (pos + kTaps)] = l;
        buf[1][(size_t) pos] = buf[1][(size_t) (pos + kTaps)] = r;
        float pk = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            const float* b = buf[(size_t) c].data() + pos;
            for (int p = 0; p < kPhases; ++p)
            {
                float acc = 0.0f;
                for (int k = 0; k < kTaps; ++k) acc += h[p][k] * b[k];
                pk = std::max (pk, std::fabs (acc));
            }
        }
        return pk;
    }

private:
    float h[kPhases][kTaps] {};
    std::array<std::array<float, 2 * kTaps>, 2> buf {};
    int pos = 0;
};

class LoudnessMeter
{
public:
    // BS.1770 K-weighting at any rate (the standard's two filters re-derived from their analogue
    // prototypes, as libebur128 does).
    static void kWeighting (double rate, Biquad& shelf, Biquad& hp)
    {
        const double pi = 3.14159265358979323846;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / rate), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0);
            shelf.b1 = (float) (2.0 * (K * K - Vh) / a0);
            shelf.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
            shelf.a1 = (float) (2.0 * (K * K - 1.0) / a0);
            shelf.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / rate), a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0f; hp.b1 = -2.0f; hp.b2 = 1.0f;
            hp.a1 = (float) (2.0 * (K * K - 1.0) / a0);
            hp.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
    }
    void prepare (double rate)
    {
        sr = rate;
        for (int c = 0; c < 2; ++c) kWeighting (rate, shelf[c], hp[c]);
        blockLen = std::max (1, (int) std::lround (0.1 * rate));
        resetAll();
    }
    void resetAll()
    {
        for (int c = 0; c < 2; ++c) { shelf[c].reset(); hp[c].reset(); }
        ring.fill (0.0); ringCount = 0; ringPos = 0;
        acc = 0.0; accN = 0;
        resetIntegrated();
        momentary = shortTerm = -70.0f;
    }
    void resetIntegrated() { binCount.fill (0); binEnergy.fill (0.0); integrated = -70.0f; }
    void push (float l, float r)
    {
        const float a = hp[0].tick (shelf[0].tick (l)), b = hp[1].tick (shelf[1].tick (r));
        acc += (double) a * a + (double) b * b;
        if (++accN >= blockLen) closeBlock();
    }
    float momentary = -70.0f, shortTerm = -70.0f, integrated = -70.0f;

private:
    static float lufs (double ms) { return (float) (-0.691 + 10.0 * std::log10 (ms + 1e-12)); }
    void closeBlock()
    {
        ring[(size_t) ringPos] = acc / (double) accN;
        ringPos = (ringPos + 1) % kRing;
        ringCount = std::min (kRing, ringCount + 1);
        acc = 0.0; accN = 0;
        auto mean = [&] (int count)
        {
            double s = 0.0;
            for (int k = 1; k <= count; ++k) s += ring[(size_t) ((ringPos - k + kRing) % kRing)];
            return s / (double) std::max (1, count);
        };
        if (ringCount >= 4)
        {
            const double m = mean (4);
            momentary = lufs (m);
            const float L = momentary;
            if (L > -70.0f)
            {
                const int b = std::min (kBins - 1, (int) ((L + 70.0f) * 10.0f));
                binCount[(size_t) b] += 1;
                binEnergy[(size_t) b] += m;
                // Integrated: absolute gate (all bins), then -10 LU relative gate.
                double e = 0.0; long n = 0;
                for (int k = 0; k < kBins; ++k) { e += binEnergy[(size_t) k]; n += binCount[(size_t) k]; }
                if (n > 0)
                {
                    const float rel = lufs (e / (double) n) - 10.0f;
                    double e2 = 0.0; long n2 = 0;
                    for (int k = 0; k < kBins; ++k)
                        if (-70.0f + ((float) k + 0.5f) * 0.1f > rel) { e2 += binEnergy[(size_t) k]; n2 += binCount[(size_t) k]; }
                    integrated = n2 > 0 ? lufs (e2 / (double) n2) : -70.0f;
                }
            }
        }
        shortTerm = ringCount >= 30 ? lufs (mean (30)) : (ringCount > 0 ? lufs (mean (ringCount)) : -70.0f);
    }
    static constexpr int kRing = 30, kBins = 760;     // -70 .. +6 LUFS in 0.1 dB
    double sr = 48000.0;
    Biquad shelf[2], hp[2];
    std::array<double, kRing> ring {};
    int ringCount = 0, ringPos = 0, blockLen = 4800, accN = 0;
    double acc = 0.0;
    std::array<long, kBins> binCount {};
    std::array<double, kBins> binEnergy {};
};
} // namespace sf
