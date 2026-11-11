#pragma once
// Offline sample-rate conversion for loaded audio (samplers, DECK): Kaiser-windowed sinc
// (beta 10, 64 zero crossings each side), cutoff at 95 % of the lower Nyquist. Passband flat to
// ~20 kHz at 44.1/48 kHz, images and aliases below -100 dB. Message / background thread only.
// Integer rates with a small ratio (44.1k -> 48k = 147:160) use precomputed polyphase filters.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace sf
{
class SincResampler
{
public:
    // Rates in Hz. Writes outN samples.
    static void process (const float* in, int inN, float* out, int outN, double inRate, double outRate)
    {
        const double ratio = inRate / outRate;
        const double s = std::min (1.0, 1.0 / ratio);
        const double c = 0.475 * s;                     // cutoff, cycles per input sample
        const double T = kZeros / (2.0 * c);            // kernel half-length in input samples
        const double scale = 2.0 * c;
        const auto& g = table();
        auto kernel = [&] (double t)
        {
            const double u = std::fabs (t * scale) * kRes;
            const int ui = (int) u;
            if (ui >= (int) g.size() - 1) return 0.0;
            return (g[(size_t) ui] + (u - ui) * (g[(size_t) ui + 1] - g[(size_t) ui])) * scale;
        };
        const long ir = std::lround (inRate), orr = std::lround (outRate);
        const bool integer = std::abs (inRate - (double) ir) < 1e-6 && std::abs (outRate - (double) orr) < 1e-6 && ir > 0 && orr > 0;
        const long gcd = integer ? std::gcd (ir, orr) : 1;
        const long L = integer ? orr / gcd : 0, M = integer ? ir / gcd : 0;
        const int left = (int) std::ceil (T);
        if (integer && L <= 4096)
        {
            // Polyphase: output i reads around input i*M/L; L distinct fractional positions.
            const int taps = 2 * left;
            std::vector<float> h ((size_t) (L * taps));
            for (long p = 0; p < L; ++p)
            {
                const double frac = (double) p / (double) L;
                for (int j = 0; j < taps; ++j) h[(size_t) (p * taps + j)] = (float) kernel (frac + left - 1 - j);
            }
            for (int i = 0; i < outN; ++i)
            {
                const long long num = (long long) i * M;
                const long base = (long) (num / L), p = (long) (num % L);
                const float* hp = h.data() + p * taps;
                const long k0 = base - left + 1;
                float acc = 0.0f;
                if (k0 >= 0 && k0 + taps <= inN)
                {
                    const float* x = in + k0;
                    for (int j = 0; j < taps; ++j) acc += hp[j] * x[j];
                }
                else
                    for (int j = 0; j < taps; ++j) { const long k = k0 + j; if (k >= 0 && k < inN) acc += hp[j] * in[k]; }
                out[i] = acc;
            }
            return;
        }
        for (int i = 0; i < outN; ++i)
        {
            const double x = i * ratio;
            const int lo = std::max (0, (int) std::ceil (x - T)), hi = std::min (inN - 1, (int) std::floor (x + T));
            double acc = 0.0;
            for (int k = lo; k <= hi; ++k) acc += kernel (x - k) * in[k];
            out[i] = (float) acc;
        }
    }

private:
    static constexpr int kZeros = 64, kRes = 512;
    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1e-17 * sum) break; }
        return sum;
    }
    static const std::vector<double>& table()
    {
        static const std::vector<double> t = []
        {
            const double beta = 10.0, pi = 3.14159265358979323846;
            std::vector<double> v ((size_t) (kZeros * kRes + 2));
            const double norm = bessel0 (beta);
            for (size_t i = 0; i < v.size(); ++i)
            {
                const double u = (double) i / kRes;                       // in zero crossings
                const double sinc = u < 1e-12 ? 1.0 : std::sin (pi * u) / (pi * u);
                const double r = u / kZeros;
                v[i] = r >= 1.0 ? 0.0 : sinc * bessel0 (beta * std::sqrt (1.0 - r * r)) / norm;
            }
            return v;
        }();
        return t;
    }
};
} // namespace sf
