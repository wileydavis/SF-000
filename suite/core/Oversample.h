#pragma once
// Oversampling: polyphase IIR halfband filters (two chains of first-order all-passes in z^-2),
// coefficients designed in closed form after Laurent de Soras' HIIR (elliptic halfband).
// At 48 kHz the 2x stage keeps 0..20 kHz flat (ripple < 1e-9 dB) and rejects everything that
// would fold into 0..20 kHz by >= 110 dB (measured in SFTests --oversampling). Minimum phase,
// a few samples of delay, no allocation after prepare().
#include <algorithm>
#include <cmath>

namespace sf
{
struct HalfbandDesign
{
    static constexpr int kMax = 16;
    double c[kMax] {};
    int n = 0;
    double attenDb = 0.0;

    // tbw: transition band as a fraction of the oversampled rate (passband edge = 0.25 - tbw).
    static HalfbandDesign make (double tbw, double targetDb)
    {
        tbw = std::max (0.005, std::min (0.24, tbw));
        double k, q;
        transition (tbw, k, q);
        HalfbandDesign d;
        int nc = 1;
        for (; nc < kMax; ++nc)
            if (atten (q, 2 * nc + 1) >= targetDb) break;
        const int order = 2 * nc + 1;
        d.n = nc;
        d.attenDb = atten (q, order);
        for (int i = 0; i < nc; ++i) d.c[i] = coef (i, k, q, order);
        return d;
    }
    // For a base rate: the 2x stage keeps 0..20 kHz, the 4x stage only has to clear the images
    // of the (already band-limited) 2x signal.
    static HalfbandDesign forStage (double baseRate, int stage, double targetDb = 110.0)
    {
        const double osr = baseRate * (stage == 0 ? 2.0 : 4.0);
        const double pass = std::min (20000.0, baseRate * 0.45);
        return make (0.25 - pass / osr, targetDb);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static void transition (double tbw, double& k, double& q)
    {
        k = std::tan ((1.0 - tbw * 2.0) * kPi / 4.0);
        k *= k;
        const double kk = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
        const double e2 = e * e, e4 = e2 * e2;
        q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    }
    static double atten (double q, int order)
    {
        const double a = 4.0 * std::exp (order * 0.5 * std::log (q));
        return -10.0 * std::log10 (a / (1.0 + a));
    }
    static double coef (int index, double k, double q, int order)
    {
        const int c = index + 1;
        double num = 0.0, den = 0.0, t;
        int i = 0, j = 1;
        do { t = std::pow (q, (double) (i * (i + 1))) * std::sin ((i * 2 + 1) * c * kPi / order) * j; num += t; j = -j; ++i; }
        while (std::fabs (t) > 1e-100 && i < 64);
        i = 1; j = -1;
        do { t = std::pow (q, (double) (i * i)) * std::cos (i * 2 * c * kPi / order) * j; den += t; j = -j; ++i; }
        while (std::fabs (t) > 1e-100 && i < 64);
        const double ww = num * std::pow (q, 0.25) / (den + 0.5);
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        return (1.0 - x) / (1.0 + x);
    }
};

// One channel of a halfband filter. Use separate objects for up and down sampling.
class Halfband
{
public:
    void set (const HalfbandDesign& d)
    {
        n = d.n;
        for (int i = 0; i < n; ++i) a[i] = (float) d.c[i];
        reset();
    }
    void reset() { std::fill (xs, xs + HalfbandDesign::kMax, 0.0f); std::fill (ys, ys + HalfbandDesign::kMax, 0.0f); }

    // Two input samples (x0 earlier, x1 later) -> one output at half the rate.
    float down (float x0, float x1)
    {
        float e = x1, o = x0;
        for (int i = 0; i < n; i += 2)
        {
            const float t = (e - ys[i]) * a[i] + xs[i];
            xs[i] = e; ys[i] = t; e = t;
        }
        for (int i = 1; i < n; i += 2)
        {
            const float t = (o - ys[i]) * a[i] + xs[i];
            xs[i] = o; ys[i] = t; o = t;
        }
        return 0.5f * (e + o);
    }
    // One input sample -> two outputs at twice the rate (y0 earlier, y1 later).
    void up (float x, float& y0, float& y1)
    {
        float e = x, o = x;
        for (int i = 0; i < n; i += 2)
        {
            const float t = (e - ys[i]) * a[i] + xs[i];
            xs[i] = e; ys[i] = t; e = t;
        }
        for (int i = 1; i < n; i += 2)
        {
            const float t = (o - ys[i]) * a[i] + xs[i];
            xs[i] = o; ys[i] = t; o = t;
        }
        y0 = e; y1 = o;
    }

private:
    int n = 0;
    float a[HalfbandDesign::kMax] {};
    float xs[HalfbandDesign::kMax] {}, ys[HalfbandDesign::kMax] {};
};

// 2x / 4x oversampler for up to 2 channels, block based. prepare() sizes nothing on the heap:
// blocks are processed in pieces of at most kBlock base-rate samples.
class Oversampler
{
public:
    static constexpr int kBlock = 64;

    void prepare (double baseRate, int factor)
    {
        fac = factor >= 4 ? 4 : 2;
        const auto d1 = HalfbandDesign::forStage (baseRate, 0), d2 = HalfbandDesign::forStage (baseRate, 1);
        for (int c = 0; c < 2; ++c)
        {
            up1[c].set (d1); dn1[c].set (d1);
            up2[c].set (d2); dn2[c].set (d2);
        }
    }
    void reset() { for (int c = 0; c < 2; ++c) { up1[c].reset(); dn1[c].reset(); up2[c].reset(); dn2[c].reset(); } }
    int factor() const { return fac; }

    // Upsample n (<= kBlock) samples of channel c into out (n * factor samples).
    void up (int c, const float* in, float* out, int n)
    {
        if (fac == 2) { for (int i = 0; i < n; ++i) up1[c].up (in[i], out[2 * i], out[2 * i + 1]); return; }
        float mid[2 * kBlock];
        for (int i = 0; i < n; ++i) up1[c].up (in[i], mid[2 * i], mid[2 * i + 1]);
        for (int i = 0; i < 2 * n; ++i) up2[c].up (mid[i], out[2 * i], out[2 * i + 1]);
    }
    // Downsample n * factor samples of channel c into out (n samples).
    void down (int c, const float* in, float* out, int n)
    {
        if (fac == 2) { for (int i = 0; i < n; ++i) out[i] = dn1[c].down (in[2 * i], in[2 * i + 1]); return; }
        float mid[2 * kBlock];
        for (int i = 0; i < 2 * n; ++i) mid[i] = dn2[c].down (in[2 * i], in[2 * i + 1]);
        for (int i = 0; i < n; ++i) out[i] = dn1[c].down (mid[2 * i], mid[2 * i + 1]);
    }

    // Run f(sample) -> sample on channel c at the oversampled rate, in place.
    template <typename F>
    void process (int c, float* x, int n, F&& f)
    {
        float buf[4 * kBlock];
        for (int i0 = 0; i0 < n; i0 += kBlock)
        {
            const int m = std::min (kBlock, n - i0);
            up (c, x + i0, buf, m);
            for (int i = 0; i < m * fac; ++i) buf[i] = f (buf[i]);
            down (c, buf, x + i0, m);
        }
    }

private:
    int fac = 2;
    Halfband up1[2], dn1[2], up2[2], dn2[2];
};

// Decimator only (instrument voices render straight at 2x).
class Decimator2x
{
public:
    void prepare (double baseRate) { const auto d = HalfbandDesign::forStage (baseRate, 0); for (auto& h : hb) h.set (d); }
    void reset() { for (auto& h : hb) h.reset(); }
    void process (int c, const float* in2x, float* out, int n) { for (int i = 0; i < n; ++i) out[i] = hb[c].down (in2x[2 * i], in2x[2 * i + 1]); }
private:
    Halfband hb[2];
};
} // namespace sf

namespace sf
{
// Linear-phase FIR halfband (Kaiser-windowed sinc, beta 10: ~100 dB), for the master chain where
// phase must not move. FirOversampler4x sizes them from the sample rate so the whole path is flat
// to 20 kHz: 2x stage 151 taps below 46 kHz (79 above), 4x stage 31 taps.
class FirHalfband
{
public:
    static constexpr int kMaxTaps = 159;
    void design (int numTaps, double transition)
    {
        // numTaps = 4k + 3; transition = width as a fraction of the oversampled rate.
        n = std::min (kMaxTaps, numTaps | 3);
        const int M = (n - 1) / 2;
        const double beta = 10.0, pi = 3.14159265358979323846;
        (void) transition;
        auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 50; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double norm = bessel0 (beta);
        for (int i = 0; i < n; ++i)
        {
            const int d = i - M;
            double h = d == 0 ? 0.5 : (d % 2 == 0 ? 0.0 : std::sin (pi * d * 0.5) / (pi * d));
            const double r = (double) d / (double) M;
            h *= bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / norm;
            taps[i] = (float) h;
        }
        nz = 0;
        for (int i = 0; i < n; ++i) if (taps[i] != 0.0f) { nzIdx[nz] = i; nzTap[nz] = taps[i]; ++nz; }
        delay = M;
        reset();
    }
    void reset() { std::fill (buf, buf + 2 * kMaxTaps, 0.0f); pos = 0; }
    // Oversampled-rate delay of one up or down pass, in oversampled samples.
    int groupDelay() const { return delay; }

    // One input -> two outputs (x2 gain for zero-stuffing).
    void up (float x, float& y0, float& y1)
    {
        y0 = push (x) * 2.0f;
        y1 = push (0.0f) * 2.0f;
    }
    // Two inputs -> one output (the second convolution result is discarded).
    float down (float x0, float x1)
    {
        pushOnly (x0);
        return push (x1);
    }

private:
    // Ring buffer duplicated so the convolution reads a contiguous span.
    void pushOnly (float x)
    {
        pos = (pos == 0 ? n : pos) - 1;
        buf[pos] = x; buf[pos + n] = x;
    }
    float push (float x)
    {
        pushOnly (x);
        const float* b = buf + pos;
        float acc = 0.0f;
        for (int i = 0; i < nz; ++i) acc += nzTap[i] * b[nzIdx[i]];
        return acc;
    }
    float taps[kMaxTaps] {}, nzTap[kMaxTaps] {};
    int nzIdx[kMaxTaps] {}, nz = 0;
    float buf[2 * kMaxTaps] {};
    int n = 3, pos = 0, delay = 1;
};

// Linear-phase Kaiser lowpass for the last 2x -> 1x step of the master clipper: flat to 20 kHz
// and >= 100 dB down from the base Nyquist up, so nothing is left near Nyquist to make
// inter-sample peaks the true-peak detector could under-read.
class FirLowpassDecimator
{
public:
    static constexpr int kMax = 511;
    void design (double pass, double stop, double osr)
    {
        const double pi = 3.14159265358979323846, A = 100.0, beta = 0.1102 * (A - 8.7);
        const double dw = 2.0 * pi * (stop - pass) / osr;
        n = (int) std::ceil ((A - 7.95) / (2.285 * std::max (1e-4, dw)));
        n = std::max (31, std::min (kMax, n | 1));
        const double fc = 0.5 * (pass + stop) / osr;
        auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 60; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const int M = (n - 1) / 2;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const int d = i - M;
            const double sinc = d == 0 ? 2.0 * fc : std::sin (2.0 * pi * fc * d) / (pi * d);
            const double r = (double) d / M;
            taps[i] = (float) (sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))));
            sum += taps[i];
        }
        for (int i = 0; i < n; ++i) taps[i] = (float) (taps[i] / sum);
        reset();
    }
    void reset() { std::fill (buf, buf + 2 * kMax, 0.0f); pos = 0; }
    float down (float x0, float x1)
    {
        push (x0); push (x1);
        const float* b = buf + pos;
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) acc += taps[i] * b[i];
        return acc;
    }
private:
    void push (float x) { pos = (pos == 0 ? n : pos) - 1; buf[pos] = x; buf[pos + n] = x; }
    float taps[kMax] {};
    float buf[2 * kMax] {};
    int n = 31, pos = 0;
};

// Stereo 4x linear-phase oversampler: FIR halfband stages up, FIR halfband + steep FIR lowpass down.
class FirOversampler4x
{
public:
    void prepare (double baseRate)
    {
        const double pass = std::min (20000.0, 0.45 * baseRate);
        // Kaiser transition (beta 10) = 92 / (2.285 (N - 1)) rad: 151 taps keep 20 kHz in the
        // passband of the 2x stage at 44.1 kHz (79 do from 46 kHz up); 31 taps at the 4x stage.
        const int n1 = baseRate < 46000.0 ? 151 : 79;
        for (int c = 0; c < 2; ++c)
        {
            u1[c].design (n1, 0.0); u2[c].design (31, 0.0); d2[c].design (31, 0.0);
            d1[c].design (pass, 0.5 * baseRate, 2.0 * baseRate);
        }
        // Latency: impulse centroid of the whole up/down path.
        double num = 0.0, den = 0.0;
        for (int i = 0; i < 1024; ++i) { const double y = process (0, i == 0 ? 1.0f : 0.0f, [] (float x) { return x; }); num += i * y; den += y; }
        lat = den != 0.0 ? num / den : 0.0;
        reset();
    }
    void reset() { for (int c = 0; c < 2; ++c) { u1[c].reset(); d1[c].reset(); u2[c].reset(); d2[c].reset(); } }
    double latency() const { return lat; }
    template <typename F>
    float process (int c, float x, F&& f)
    {
        float a0, a1, b[4];
        u1[c].up (x, a0, a1);
        u2[c].up (a0, b[0], b[1]);
        u2[c].up (a1, b[2], b[3]);
        for (auto& v : b) v = f (v);
        const float m0 = d2[c].down (b[0], b[1]);          // in order: the filters have state
        const float m1 = d2[c].down (b[2], b[3]);
        return d1[c].down (m0, m1);
    }
private:
    FirHalfband u1[2], u2[2], d2[2];
    FirLowpassDecimator d1[2];
    double lat = 0.0;
};
} // namespace sf
