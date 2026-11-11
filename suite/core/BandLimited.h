#pragma once
// Band-limited oscillators for voices rendered at an oversampled rate (2x).
// Mipmap: one 4096-point single-cycle table per octave, each holding only the harmonics that stay
// below 0.75 x the oversampled rate for the highest note it serves. Everything that would fold
// back lands above 0.25 x that rate, inside the stopband of the 2x decimator: the audible band is
// alias-free (SFTests --oversampling measures it). Read with 4-point Hermite interpolation.
// Tables are built once per rate and shared by every instance in the process.
#include "Dsp.h"
#include <complex>
#include <map>
#include <memory>
#include <mutex>

namespace sf
{
// Phase wrap that holds for any increment (an extreme note can step more than a cycle per sample).
inline void wrap01 (double& p)
{
    if (p >= 1.0) { p -= 1.0; if (p >= 1.0) p -= std::floor (p); }
}

class Mipmap
{
public:
    static constexpr int kSize = 4096, kOct = 12;
    static constexpr float kLowestTop = 27.5f;     // table 0 serves fundamentals up to 27.5 Hz

    // amp/phase per harmonic (1-based in the vectors' index + 1); sine phase.
    void build (const std::vector<float>& amp, const std::vector<float>& phase, double osr, int maxHarm = 1 << 30)
    {
        const double limit = 0.75 * osr;
        for (int k = 0; k < kOct; ++k)
        {
            const double top = kLowestTop * std::pow (2.0, (double) k);
            int h = (int) std::floor (limit / top);
            h = std::max (1, std::min ({ h, kSize / 4, (int) amp.size(), maxHarm }));
            synth (k, amp, phase, h);
        }
        tab[kOct].assign ((size_t) kSize + 4, 0.0f);       // silent
        rate = osr;
    }

    // Table for a fundamental of f Hz. At or above 0.45 x the oversampled rate there is no
    // harmonic left that can be played without folding: the silent table.
    int octaveFor (float f) const
    {
        if (rate > 0.0 && (double) f >= 0.45 * rate) return kOct;
        int e;
        std::frexp (std::max (1.0f, f * 1.03f) / kLowestTop, &e);   // f/27.5 in [2^(e-1), 2^e)
        return std::max (0, std::min (kOct - 1, e));
    }
    // p: phase 0..1 (anything else is wrapped: never reads outside the table)
    float read (int oct, double p) const
    {
        if (p >= 1.0 || p < 0.0) p -= std::floor (p);
        const float* t = tab[oct].data() + 1;
        const double x = p * kSize;
        const int i = (int) x;
        const float f = (float) (x - i);
        const float xm1 = t[i - 1], x0 = t[i], x1 = t[i + 1], x2 = t[i + 2];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    // Harmonics 1..H of one cycle sampled at the middles of N equal steps (N a power of two), as
    // build() takes them (amplitude, sine phase).
    static void harmonicsOf (const std::vector<double>& cycle, int H, std::vector<float>& amp, std::vector<float>& phase)
    {
        const size_t n = cycle.size();
        std::vector<std::complex<double>> X (n);
        for (size_t i = 0; i < n; ++i) X[i] = cycle[i];
        fft (X, false);
        amp.assign ((size_t) H, 0.0f); phase.assign ((size_t) H, 0.0f);
        for (int h = 1; h <= H && (size_t) h < n / 2; ++h)
        {
            // c_h = X[h] / N, moved back half a step (the samples sit at the steps' middles).
            const auto c = X[(size_t) h] / (double) n * std::polar (1.0, -3.14159265358979323846 * h / (double) n);
            amp[(size_t) h - 1] = (float) (2.0 * std::abs (c));
            phase[(size_t) h - 1] = (float) (std::arg (c) + 0.5 * 3.14159265358979323846);
        }
    }

    // The FFT itself (radix 2, in place), for analyses next to these tables.
    static void transform (std::vector<std::complex<double>>& a, bool inverse) { fft (a, inverse); }

    // The same from `cycles` periods in a row (cycles x N points, N a power of two, at the steps'
    // middles), Hann-windowed [Nov 10]: a cycle cut out of a recording doesn't join up with itself,
    // and a single cycle's FFT would hear the jump as a buzz of every harmonic; across several
    // periods the window takes the ends away and harmonic h is bin cycles x h.
    static void harmonicsOfCycles (const std::vector<double>& x, int cycles, int H, std::vector<float>& amp, std::vector<float>& phase)
    {
        const size_t n = x.size(), per = n / (size_t) std::max (1, cycles);
        std::vector<std::complex<double>> X (n);
        for (size_t i = 0; i < n; ++i) X[i] = x[i] * (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * ((double) i + 0.5) / (double) n));
        fft (X, false);
        amp.assign ((size_t) H, 0.0f); phase.assign ((size_t) H, 0.0f);
        for (int h = 1; h <= H && (size_t) (h * cycles) < n / 2; ++h)
        {
            // The window's coherent gain is a half; half a step back, as harmonicsOf.
            const auto c = X[(size_t) (h * cycles)] / (0.5 * (double) n) * std::polar (1.0, -3.14159265358979323846 * h / (double) per);
            amp[(size_t) h - 1] = (float) (2.0 * std::abs (c));
            phase[(size_t) h - 1] = (float) (std::arg (c) + 0.5 * 3.14159265358979323846);
        }
    }

private:
    void synth (int k, const std::vector<float>& amp, const std::vector<float>& phase, int harmonics)
    {
        // Inverse DFT by radix-2 FFT (double precision).
        std::vector<std::complex<double>> X ((size_t) kSize);
        for (int h = 1; h <= harmonics; ++h)
        {
            const double a = amp[(size_t) h - 1], ph = h - 1 < (int) phase.size() ? phase[(size_t) h - 1] : 0.0;
            // a sin(wt + ph) = a (e^{j(wt+ph)} - e^{-j(wt+ph)}) / 2j
            const auto v = std::polar (a * 0.5, ph) / std::complex<double> (0.0, 1.0);
            X[(size_t) h] += v;
            X[(size_t) (kSize - h)] += std::conj (v);
        }
        fft (X, true);
        auto& t = tab[k];
        t.assign ((size_t) kSize + 4, 0.0f);
        for (int i = 0; i < kSize; ++i) t[(size_t) i + 1] = (float) X[(size_t) i].real();
        t[0] = t[(size_t) kSize];                  // [-1]
        t[(size_t) kSize + 1] = t[1];              // [N]
        t[(size_t) kSize + 2] = t[2];              // [N+1]
        t[(size_t) kSize + 3] = t[3];
    }
    static void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * 3.14159265358979323846 / (double) len * (inverse ? 1.0 : -1.0);
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }
    std::vector<float> tab[kOct + 1];
    double rate = 0.0;
};

// The shared table set for one oversampled rate.
struct WaveSet
{
    Mipmap saw, square, tri;
    Mipmap square303;         // the TB-303's square (its waveshaper on the saw), ACID 303-TYPE's
    Mipmap frame[4];          // WAVETABLE engine frames: sine, saw 32, square 32, vowel
    double osr = 0.0;

    static std::shared_ptr<const WaveSet> get (double osr)
    {
        static std::mutex m;
        static std::map<long, std::shared_ptr<const WaveSet>> cache;
        std::lock_guard<std::mutex> lock (m);
        const long key = std::lround (osr);
        if (auto it = cache.find (key); it != cache.end()) return it->second;
        auto w = std::make_shared<WaveSet>();
        w->build (osr);
        cache[key] = w;
        return w;
    }

private:
    void build (double rate)
    {
        osr = rate;
        constexpr int H = Mipmap::kSize / 4;
        const float pi = 3.14159265358979f;
        std::vector<float> a ((size_t) H), p ((size_t) H, 0.0f);
        // Rising saw 2t-1 = -(2/pi) sum sin(2 pi h t) / h
        for (int h = 1; h <= H; ++h) { a[(size_t) h - 1] = 2.0f / (pi * (float) h); p[(size_t) h - 1] = pi; }
        saw.build (a, p, rate);
        // Square: +1 for t < 0.5
        for (int h = 1; h <= H; ++h) { a[(size_t) h - 1] = (h & 1) ? 4.0f / (pi * (float) h) : 0.0f; p[(size_t) h - 1] = 0.0f; }
        square.build (a, p, rate);
        // Triangle from 0, rising
        for (int h = 1; h <= H; ++h)
        {
            a[(size_t) h - 1] = (h & 1) ? 8.0f / (pi * pi * (float) (h * h)) : 0.0f;
            p[(size_t) h - 1] = ((h - 1) / 2) % 2 == 0 ? 0.0f : pi;
        }
        tri.build (a, p, rate);
        // The TB-303's square: its waveshaper (a transistor's soft knee) on the rising saw,
        // tanh (4.5 (saw + 0.1)): high a little over half the cycle, one edge the saw's reset
        // (sharp), the other the knee's (soft). Shaped here, before band-limiting, so it can't
        // alias as the shaper on a band-limited saw does.
        {
            constexpr int N = 1 << 16;
            std::vector<double> cyc ((size_t) N);
            for (int i = 0; i < N; ++i) cyc[(size_t) i] = std::tanh (4.5 * (2.0 * ((double) i + 0.5) / N - 0.9));
            std::vector<float> a3, p3;
            Mipmap::harmonicsOf (cyc, H, a3, p3);
            square303.build (a3, p3, rate);
        }
        // WAVETABLE frames (32 harmonics, as the original 2048-point tables).
        std::vector<float> f ((size_t) 32), fp ((size_t) 32, 0.0f);
        std::fill (f.begin(), f.end(), 0.0f); f[0] = 1.0f;
        frame[0].build (f, fp, rate);
        for (int h = 1; h <= 32; ++h) f[(size_t) h - 1] = 0.55f / (float) h;
        frame[1].build (f, fp, rate);
        for (int h = 1; h <= 32; ++h) f[(size_t) h - 1] = (h & 1) ? 0.9f / (float) h : 0.0f;
        frame[2].build (f, fp, rate);
        for (int h = 1; h <= 32; ++h)
        {
            const float hz = (float) h * 130.0f;
            const float formant = std::exp (-std::pow ((hz - 700.0f) / 300.0f, 2.0f)) + 0.6f * std::exp (-std::pow ((hz - 1200.0f) / 300.0f, 2.0f));
            f[(size_t) h - 1] = (formant + 0.05f / (float) h) * 0.5f;
        }
        frame[3].build (f, fp, rate);
    }
};

// 2-sample polyBLEP residual (for hard sync resets at the oversampled rate).
inline float polyBlep (double t, double dt)
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

// 4-point Hermite read of a sample buffer channel at a fractional position (pos >= 0).
inline float hermiteAt (const float* x, int len, double pos)
{
    const int i = (int) pos;
    const float f = (float) (pos - i);
    auto at = [&] (int k) { return k < 0 ? x[0] : (k >= len ? 0.0f : x[k]); };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// Band-limited sample read for any playback rate (input samples per output sample).
// rate <= 1: Hermite (the content already fits under the output's Nyquist).
// rate  > 1: a Kaiser-windowed sinc low-pass at 0.9 / rate of the input's Nyquist, so content that
// would land above the output's Nyquist is removed instead of folding back (a sample read an
// octave up used to alias at full level). 8 zero crossings a side: about 18 x rate taps.
class SincKernel
{
public:
    static constexpr int kZero = 8, kRes = 512;
    static const SincKernel& get() { static const SincKernel k; return k; }
    // Window-weighted sinc at u zero crossings from the centre (|u| < kZero), table-interpolated.
    float at (float u) const
    {
        u = std::fabs (u) * (float) kRes;
        const int i = (int) u;
        if (i >= kZero * kRes) return 0.0f;
        const float f = u - (float) i;
        return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
    }
private:
    SincKernel()
    {
        const double beta = 8.0, pi = 3.14159265358979323846;
        auto i0 = [] (double x) { double s = 1.0, term = 1.0; for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); s += term; } return s; };
        t.resize ((size_t) kZero * kRes + 2);
        for (size_t i = 0; i < t.size(); ++i)
        {
            const double u = (double) i / kRes;
            const double sinc = u < 1e-12 ? 1.0 : std::sin (pi * u) / (pi * u);
            const double r = u / kZero;
            t[i] = r >= 1.0 ? 0.0f : (float) (sinc * i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta));
        }
    }
    std::vector<float> t;
};

inline float sampleAt (const float* x, int len, double pos, double rate)
{
    if (rate <= 1.0) return hermiteAt (x, len, pos);
    const auto& K = SincKernel::get();
    const double fc = 0.9 / rate;                          // cutoff, in units of the input's Nyquist
    const double half = SincKernel::kZero / fc;            // kernel half-width in input samples
    const int lo = std::max (0, (int) std::ceil (pos - half)), hi = std::min (len - 1, (int) std::floor (pos + half));
    double acc = 0.0, wsum = 0.0;
    for (int k = lo; k <= hi; ++k)
    {
        const float w = K.at ((float) ((k - pos) * fc));
        acc += (double) w * x[k];
        wsum += w;
    }
    // Normalised by the kernel's own sum: unity gain at DC whatever the fraction (and taper at the edges).
    const double full = 1.0 / fc;                           // the sum a complete kernel has (sum of sinc = 1/fc)
    return (float) (acc / std::max (wsum, full * 0.25));
}
} // namespace sf
