#pragma once
// (SF-000) Wavetables: a table is many frames, each one cycle, and a place along it reads between two of them.
// Every frame is band-limited an octave at a time, as Mipmap's single waves are (the same levels, the same rule for
// which level a note reads), so a table read at any pitch is as clean as the oscillators beside it. A level holds
// only the harmonics that stay under 0.75 x the oversampled rate for the highest note it serves, and is only as long
// as those need (eight points to the highest harmonic, 64 to 2048), so sixty-four frames are about three megabytes.
// WavetableSet is the tables that come with the instrument, built once for a rate and shared by every instance.
// None of this is in the suite.
#include "BandLimited.h"
#include <cmath>

namespace sf
{
class Wavetable
{
public:
    static constexpr int kLevels = Mipmap::kOct;        // and a silent one after them
    static constexpr int kMaxSize = 2048, kMaxHarm = kMaxSize / 4;

    int frames() const { return n; }
    juce::String name;
    std::vector<float> level;       // a gain a frame (1, but where a recording's own level is kept)

    // amp[f], phase[f]: frame f's harmonics (1-based in the index + 1), sine phase, as Mipmap::build takes them.
    void build (const std::vector<std::vector<float>>& amp, const std::vector<std::vector<float>>& phase, double osr)
    {
        rate = osr;
        n = (int) amp.size();
        // the levels: how many harmonics each may hold, and so how long it is; levels that come out the same share one
        int at = 0, harm[kLevels + 1];
        for (int k = 0; k <= kLevels; ++k)
        {
            const double top = Mipmap::kLowestTop * std::pow (2.0, (double) k);
            harm[k] = k == kLevels ? 0 : std::max (1, std::min (kMaxHarm, (int) std::floor (0.75 * osr / top)));
            int size = 64; while (size < 8 * harm[k] && size < kMaxSize) size <<= 1;
            sizeOf[k] = size;
            if (k > 0 && harm[k] == harm[k - 1] && sizeOf[k] == sizeOf[k - 1]) { offOf[k] = offOf[k - 1]; continue; }
            offOf[k] = at; at += size + 4;
        }
        stride = (size_t) at;
        data.assign (stride * (size_t) std::max (0, n), 0.0f);
        level.assign ((size_t) std::max (0, n), 1.0f);
        std::vector<std::complex<double>> X;
        for (int f = 0; f < n; ++f)
            for (int k = 0; k < kLevels; ++k)
            {
                if (k > 0 && offOf[k] == offOf[k - 1]) continue;
                const int size = sizeOf[k], H = std::min (harm[k], (int) amp[(size_t) f].size());
                X.assign ((size_t) size, {});
                for (int h = 1; h <= H && h < size / 2; ++h)
                {
                    const double a = amp[(size_t) f][(size_t) h - 1], ph = h - 1 < (int) phase[(size_t) f].size() ? phase[(size_t) f][(size_t) h - 1] : 0.0;
                    const auto v = std::polar (a * 0.5, ph) / std::complex<double> (0.0, 1.0);
                    X[(size_t) h] += v; X[(size_t) (size - h)] += std::conj (v);
                }
                Mipmap::transform (X, true);
                float* t = data.data() + (size_t) f * stride + (size_t) offOf[k];
                for (int i = 0; i < size; ++i) t[i + 1] = (float) X[(size_t) i].real();
                t[0] = t[size]; t[size + 1] = t[1]; t[size + 2] = t[2]; t[size + 3] = t[3];
            }
    }
    // The level for a fundamental of f Hz (Mipmap's rule): the silent one where nothing could be played without folding.
    int octaveFor (float f) const
    {
        if (rate > 0.0 && (double) f >= 0.45 * rate) return kLevels;
        int e;
        std::frexp (std::max (1.0f, f * 1.03f) / Mipmap::kLowestTop, &e);
        return std::max (0, std::min (kLevels - 1, e));
    }
    // p: phase 0..1 (anything else is wrapped)
    float read (int frame, int oct, double p) const
    {
        if (p >= 1.0 || p < 0.0) p -= std::floor (p);
        const int size = sizeOf[oct];
        const float* t = data.data() + (size_t) frame * stride + (size_t) offOf[oct] + 1;
        const double x = p * size;
        const int i = (int) x;
        const float f = (float) (x - i);
        const float xm1 = t[i - 1], x0 = t[i], x1 = t[i + 1], x2 = t[i + 2];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    // Between two frames: a place along the table, 0 its first frame to 1 its last.
    float at (float place, int oct, double p) const
    {
        const float pos = std::max (0.0f, std::min (1.0f, place)) * (float) (n - 1);
        const int f0 = std::min (std::max (0, n - 2), (int) pos);
        if (n < 2) return read (0, oct, p);
        const float a = read (f0, oct, p);
        return a + (pos - (float) f0) * (read (f0 + 1, oct, p) - a);
    }
    float levelAt (float place) const
    {
        if (n < 2) return n == 1 ? level[0] : 1.0f;
        const float pos = std::max (0.0f, std::min (1.0f, place)) * (float) (n - 1);
        const int f0 = std::min (n - 2, (int) pos);
        return level[(size_t) f0] + (pos - (float) f0) * (level[(size_t) f0 + 1] - level[(size_t) f0]);
    }

    // ---- making frames
    // A frame's loudness brought to one figure (its harmonics' RMS).
    static double rmsOf (const std::vector<float>& a) { double e = 0.0; for (auto v : a) e += 0.5 * (double) v * v; return std::sqrt (e); }
    static void toRms (std::vector<float>& a, double want = 0.5) { const double r = rmsOf (a); if (r > 1.0e-9) for (auto& v : a) v = (float) (v * want / r); }
    // The harmonics of a cycle made of straight pieces (a saw, a square, a pulse, a synced saw), exactly: no sampling,
    // so none of the folding a sampled edge has. A piece runs from (a, ya) to (b, yb), 0 <= a < b <= 1.
    struct Piece { double a, b, ya, yb; };
    static void piecesToHarmonics (const std::vector<Piece>& pieces, int H, std::vector<float>& amp, std::vector<float>& phase)
    {
        amp.assign ((size_t) H, 0.0f); phase.assign ((size_t) H, 0.0f);
        const std::complex<double> j (0.0, 1.0);
        for (int h = 1; h <= H; ++h)
        {
            const double w = 2.0 * 3.14159265358979323846 * h;
            std::complex<double> c (0.0, 0.0);
            for (auto& s : pieces)
            {
                const double al = (s.yb - s.ya) / (s.b - s.a), be = s.ya - al * s.a;
                auto F = [&] (double t) { return std::exp (-j * (w * t)) * ((al * t + be) * (j / w) + al / (w * w)); };
                c += F (s.b) - F (s.a);
            }
            amp[(size_t) h - 1] = (float) (2.0 * std::abs (c));
            phase[(size_t) h - 1] = (float) (std::arg (c) + 0.5 * 3.14159265358979323846);
        }
    }
    // The harmonics of a smooth cycle given as a function of 0..1 (sampled at 4096 points: for waves with no edges).
    template <typename Fn> static void smoothToHarmonics (Fn fn, int H, std::vector<float>& amp, std::vector<float>& phase)
    {
        constexpr int N = 4096;
        std::vector<double> cyc ((size_t) N);
        for (int i = 0; i < N; ++i) cyc[(size_t) i] = fn (((double) i + 0.5) / N);
        Mipmap::harmonicsOf (cyc, H, amp, phase);
    }

private:
    int n = 0;
    size_t stride = 0;
    int sizeOf[kLevels + 1] = {}, offOf[kLevels + 1] = {};
    std::vector<float> data;
    double rate = 0.0;
};

// The tables that come with the instrument. Sixty-four frames each, 512 harmonics where a frame has them.
struct WavetableSet
{
    static constexpr int kTables = 11, kFrames = 64, kH = Wavetable::kMaxHarm;
    Wavetable t[kTables];
    static const char* nameOf (int k)
    {
        static const char* const names[kTables] = { "BASIC", "HARMONIC", "PWM", "SYNC", "FORMANT", "FM", "FOLD", "DIGITAL", "ORGAN", "BASS", "COMB" };
        return names[std::max (0, std::min (kTables - 1, k))];
    }
    static std::shared_ptr<const WavetableSet> get (double osr)
    {
        static std::mutex m;
        static std::map<long, std::shared_ptr<const WavetableSet>> cache;
        std::lock_guard<std::mutex> lock (m);
        const long key = std::lround (osr);
        if (auto it = cache.find (key); it != cache.end()) return it->second;
        auto w = std::make_shared<WavetableSet>();
        w->build (osr);
        cache[key] = w;
        return w;
    }

private:
    using Frames = std::vector<std::vector<float>>;
    void build (double osr)
    {
        const double pi = 3.14159265358979323846;
        // (BASIC's five waves)
        std::vector<float> wa[5], wp[5];
        for (auto& x : wa) x.assign ((size_t) kH, 0.0f);
        for (auto& x : wp) x.assign ((size_t) kH, 0.0f);
        wa[0][0] = 1.0f;
        Wavetable::piecesToHarmonics ({ { 0.0, 0.25, 0.0, 1.0 }, { 0.25, 0.75, 1.0, -1.0 }, { 0.75, 1.0, -1.0, 0.0 } }, kH, wa[1], wp[1]);
        Wavetable::piecesToHarmonics ({ { 0.0, 0.5, 0.0, 1.0 }, { 0.5, 1.0, -1.0, 0.0 } }, kH, wa[2], wp[2]);
        Wavetable::piecesToHarmonics ({ { 0.0, 0.5, 1.0, 1.0 }, { 0.5, 1.0, -1.0, -1.0 } }, kH, wa[3], wp[3]);
        Wavetable::piecesToHarmonics ({ { 0.0, 0.1, 1.0, 1.0 }, { 0.1, 1.0, -1.0, -1.0 } }, kH, wa[4], wp[4]);
        for (auto& x : wa) Wavetable::toRms (x);
        for (int k = 0; k < kTables; ++k)
        {
            Frames amp ((size_t) kFrames), ph ((size_t) kFrames);
            for (int f = 0; f < kFrames; ++f)
            {
                const double u = (double) f / (double) (kFrames - 1);
                auto& a = amp[(size_t) f]; auto& p = ph[(size_t) f];
                a.assign ((size_t) kH, 0.0f); p.assign ((size_t) kH, 0.0f);
                switch (k)
                {
                    case 0:     // BASIC: a sine, a triangle, a saw, a square, a narrow pulse, each crossing into the next
                    {
                        const double at = u * 4.0; const int i0 = std::min (3, (int) at); const double fr = at - i0;
                        for (int h = 0; h < kH; ++h)
                        {
                            // (the waves themselves crossed: each harmonic as the sum of the two, by how far along)
                            const auto c0 = std::polar ((double) wa[i0][(size_t) h], (double) wp[i0][(size_t) h]), c1 = std::polar ((double) wa[i0 + 1][(size_t) h], (double) wp[i0 + 1][(size_t) h]);
                            const auto c = c0 * (1.0 - fr) + c1 * fr;
                            a[(size_t) h] = (float) std::abs (c); p[(size_t) h] = (float) std::arg (c);
                        }
                        break;
                    }
                    case 1:     // HARMONIC: a saw opened from its fundamental to all of it, with a peak where it is cut
                    {
                        const double hc = std::pow (2.0, u * 8.5);
                        for (int h = 1; h <= kH; ++h)
                        {
                            const double x = (double) h / hc, d = ((double) h - hc) / (0.2 * hc + 0.6);
                            a[(size_t) h - 1] = (float) (1.0 / h / std::sqrt (1.0 + x * x * x * x * x * x) * (1.0 + 2.5 * std::exp (-d * d)));
                            p[(size_t) h - 1] = (float) pi;
                        }
                        break;
                    }
                    case 2:     // PWM: a pulse, from a square down to a sliver
                    {
                        const double d = 0.5 - 0.46 * u;
                        Wavetable::piecesToHarmonics ({ { 0.0, d, 1.0, 1.0 }, { d, 1.0, -1.0, -1.0 } }, kH, a, p);
                        break;
                    }
                    case 3:     // SYNC: a saw restarted by the note, running from once to eight times as fast as it
                    {
                        const double r = std::pow (8.0, u);
                        std::vector<Wavetable::Piece> pc;
                        for (int m = 0; (double) m / r < 1.0 - 1.0e-9; ++m)
                        {
                            const double a0 = (double) m / r, b0 = std::min (1.0, (double) (m + 1) / r);
                            pc.push_back ({ a0, b0, -1.0, -1.0 + 2.0 * (b0 - a0) * r });
                        }
                        Wavetable::piecesToHarmonics (pc, kH, a, p);
                        break;
                    }
                    case 4:     // FORMANT: a voice's three resonances on a saw, through OO, OH, AH, EH, EE (as at 110 Hz)
                    {
                        static const double V[5][3] = { { 325, 700, 2530 }, { 450, 800, 2830 }, { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 } };
                        const double at = u * 4.0; const int i0 = std::min (3, (int) at); const double fr = at - i0;
                        const double B[3] = { 90.0, 110.0, 170.0 }, G[3] = { 1.0, 0.6, 0.3 };
                        for (int h = 1; h <= kH; ++h)
                        {
                            const double hz = 110.0 * h;
                            double g = 0.0;
                            for (int q = 0; q < 3; ++q) { const double F = V[i0][q] + (V[i0 + 1][q] - V[i0][q]) * fr, x = (hz - F) / B[q]; g += G[q] * std::exp (-0.5 * x * x); }
                            a[(size_t) h - 1] = (float) (std::pow ((double) h, -0.6) * g + 0.02 / h);
                        }
                        break;
                    }
                    case 5:     // FM: a sine bent by another an octave up, from none of it to a great deal
                        Wavetable::smoothToHarmonics ([&] (double x) { return std::sin (2.0 * pi * x + 7.0 * u * std::sin (4.0 * pi * x)); }, kH, a, p);
                        break;
                    case 6:     // FOLD: a sine driven into a folder, harder and harder
                        Wavetable::smoothToHarmonics ([&] (double x) { return std::sin (0.5 * pi * (1.0 + 9.0 * u) * std::sin (2.0 * pi * x)); }, kH, a, p);
                        break;
                    case 7:     // DIGITAL: nine sparse sets of harmonics, each crossing into the next (always the same nine)
                    {
                        const double at = u * 8.0; const int i0 = std::min (7, (int) at); const double fr = at - i0;
                        auto set = [] (int which, int h) -> double
                        {
                            if (h == 1) return 1.0;
                            if (h > 96) return 0.0;
                            juce::uint32 x = 2166136261u ^ (juce::uint32) (which * 7919 + h * 104729);
                            x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15; x *= 0x27d4eb2du; x ^= x >> 13;
                            const double r = (double) (x & 0xffffu) / 65535.0, keep = 0.10 + 0.05 * which;
                            return r < keep ? (0.4 + 0.6 * (double) ((x >> 16) & 0xffu) / 255.0) * std::pow ((double) h, -0.6) : 0.0;
                        };
                        for (int h = 1; h <= kH; ++h) a[(size_t) h - 1] = (float) (set (i0, h) * (1.0 - fr) + set (i0 + 1, h) * fr);
                        break;
                    }
                    case 8:     // ORGAN: a sine, and the drawbars pulled out one after another
                    {
                        static const int bar[10] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16 };
                        for (int q = 0; q < 10; ++q) a[(size_t) bar[q] - 1] = (float) (std::pow (0.88, q) * std::max (0.0, std::min (1.0, u * 9.0 - (double) q + 1.0)));
                        break;
                    }
                    case 9:     // BASS: a sub that grows a second harmonic and is driven square
                        Wavetable::smoothToHarmonics ([&] (double x) { return std::tanh ((1.0 + 9.0 * u * u) * (std::sin (2.0 * pi * x) + 0.6 * u * std::sin (4.0 * pi * x + 0.6))); }, kH, a, p);
                        break;
                    default:    // COMB: a saw through a comb whose teeth move apart: hollow, then nasal
                    {
                        const double tau = 0.33 * std::pow (0.06, u);
                        for (int h = 1; h <= kH; ++h) { const double c = std::cos (pi * h * tau); a[(size_t) h - 1] = (float) ((0.1 + 0.9 * c * c) / h); p[(size_t) h - 1] = (float) pi; }
                        break;
                    }
                }
                Wavetable::toRms (a);
            }
            t[k].build (amp, ph, osr);
            t[k].name = nameOf (k);
        }
    }
};
}
