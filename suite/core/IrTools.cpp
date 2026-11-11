#include "IrTools.h"
#include "Dsp.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>

namespace sf
{
namespace ir
{
namespace
{
constexpr double kPiD = 3.14159265358979323846;
constexpr double kF1 = 20.0, kF2 = 20000.0;     // the sweep, in Hz (the same at every sample rate)
constexpr double kSoftHz = 60.0;                 // under this the sweep comes in softer (2nd-order high-pass shape)

int orderFor (int64_t n)
{
    int o = 1;
    while (((int64_t) 1 << o) < n) ++o;
    return o;
}

struct Spectrum { std::vector<float> re, im; };

// Real FFT of x zero-padded to 2^order: bins 0 .. 2^(order-1), split.
Spectrum rfft (const float* x, int n, int order)
{
    const int N = 1 << order;
    std::vector<float> w (2 * (size_t) N, 0.0f);
    std::copy (x, x + std::min (n, N), w.begin());
    juce::dsp::FFT f (order);
    f.performRealOnlyForwardTransform (w.data(), true);
    Spectrum s;
    s.re.resize ((size_t) N / 2 + 1); s.im.resize (s.re.size());
    for (size_t k = 0; k < s.re.size(); ++k) { s.re[k] = w[2 * k]; s.im[k] = w[2 * k + 1]; }
    return s;
}

std::vector<float> irfft (const Spectrum& s, int order)
{
    const int N = 1 << order;
    std::vector<float> w (2 * (size_t) N, 0.0f);
    for (size_t k = 0; k < s.re.size(); ++k) { w[2 * k] = s.re[k]; w[2 * k + 1] = s.im[k]; }
    juce::dsp::FFT f (order);
    f.performRealOnlyInverseTransform (w.data());
    w.resize ((size_t) N);
    return w;
}

// The regularised inverse of the sweep (X* / (|X|^2 + eps)), eps large outside 30 Hz - 18 kHz.
// sweepRate: the rate the sweep is generated at (the recorder's rate, times the playback drift).
Spectrum inverseSweep (double sweepRate, int order)
{
    const double sr = sweepRate;
    const auto s = sweep (sr);
    auto X = rfft (s.data(), (int) s.size(), order);
    const int N = 1 << order;
    float maxX2 = 0.0f;
    for (size_t k = 0; k < X.re.size(); ++k) maxX2 = std::max (maxX2, X.re[k] * X.re[k] + X.im[k] * X.im[k]);
    Spectrum inv;
    inv.re.resize (X.re.size()); inv.im.resize (X.re.size());
    for (size_t k = 0; k < X.re.size(); ++k)
    {
        const double f = (double) k * sr / N;
        // Out of band: 0 between 30 Hz and 18 kHz, a raised cosine (in octaves) up to 1 at 20 Hz / 20 kHz.
        double out = 0.0;
        if (f < 30.0) out = f <= kF1 ? 1.0 : 0.5 + 0.5 * std::cos (kPiD * std::log2 (f / kF1) / std::log2 (30.0 / kF1));
        else if (f > 18000.0) out = f >= kF2 ? 1.0 : 0.5 - 0.5 * std::cos (kPiD * std::log2 (f / 18000.0) / std::log2 (kF2 / 18000.0));
        const double eps = maxX2 * (1.0e-6 + out);
        const double re = X.re[k], im = X.im[k], d = re * re + im * im + eps;
        inv.re[k] = (float) (re / d);
        inv.im[k] = (float) (-im / d);
    }
    return inv;
}

std::vector<float> deconvolve (const float* x, int n, const Spectrum& inv, int order)
{
    auto Y = rfft (x, n, order);
    for (size_t k = 0; k < Y.re.size(); ++k)
    {
        const float a = Y.re[k], b = Y.im[k], c = inv.re[k], d = inv.im[k];
        Y.re[k] = a * c - b * d;
        Y.im[k] = a * d + b * c;
    }
    return irfft (Y, order);
}

// Peaks of |h| at least `rangeDb` below the largest, at least minGap apart, in [0, limit).
std::vector<int> findPeaks (const std::vector<float>& h, int limit, float rangeDb, int minGap)
{
    float top = 0.0f;
    for (int i = 0; i < limit; ++i) top = std::max (top, std::fabs (h[(size_t) i]));
    const float thr = top * std::pow (10.0f, -rangeDb / 20.0f);
    std::vector<int> cand;
    for (int i = 1; i + 1 < limit; ++i)
    {
        const float v = std::fabs (h[(size_t) i]);
        if (v >= thr && v >= std::fabs (h[(size_t) i - 1]) && v >= std::fabs (h[(size_t) i + 1])) cand.push_back (i);
    }
    // Strongest first; drop anything within minGap of a stronger peak.
    std::sort (cand.begin(), cand.end(), [&] (int a, int b) { return std::fabs (h[(size_t) a]) > std::fabs (h[(size_t) b]); });
    std::vector<int> out;
    for (int c : cand)
    {
        bool near = false;
        for (int o : out) if (std::abs (o - c) < minGap) { near = true; break; }
        if (! near) out.push_back (c);
        if (out.size() >= 8) break;
    }
    std::sort (out.begin(), out.end());
    return out;
}

// Kaiser-windowed sinc (beta 8), 16 zero crossings, 512 points per crossing.
constexpr int kZeros = 16, kRes = 512;
const std::vector<float>& sincTable()
{
    static const std::vector<float> table = []
    {
        std::vector<float> t ((size_t) kZeros * kRes + 2);
        auto i0 = [] (double x) { double s = 1.0, k = 1.0; for (int j = 1; j < 50; ++j) { k *= (x / (2.0 * j)) * (x / (2.0 * j)); s += k; } return s; };
        const double beta = 8.0;
        for (size_t j = 0; j < t.size(); ++j)
        {
            const double x = (double) j / kRes;                 // in zero crossings
            const double r = std::min (1.0, x / kZeros);
            const double sinc = x < 1.0e-12 ? 1.0 : std::sin (kPiD * x) / (kPiD * x);
            t[j] = (float) (sinc * i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta));
        }
        return t;
    }();
    return table;
}

// h at a fractional position (band-limited interpolation, 95 % of the band).
float readFrac (const std::vector<float>& h, double pos)
{
    const auto& t = sincTable();
    const double c = 0.475, T = kZeros / (2.0 * c), scale = 2.0 * c;
    const int lo = std::max (0, (int) std::ceil (pos - T)), hi = std::min ((int) h.size() - 1, (int) std::floor (pos + T));
    double acc = 0.0;
    for (int k = lo; k <= hi; ++k)
    {
        const double u = std::fabs ((pos - k) * scale) * kRes;
        const int ui = (int) u;
        if (ui >= (int) t.size() - 1) continue;
        acc += (t[(size_t) ui] + (u - ui) * (t[(size_t) ui + 1] - t[(size_t) ui])) * h[(size_t) k];
    }
    return (float) (acc * scale);
}

double refine (const std::vector<float>& h, int i)
{
    if (i <= 0 || i + 1 >= (int) h.size()) return i;
    const double a = std::fabs (h[(size_t) i - 1]), b = std::fabs (h[(size_t) i]), c = std::fabs (h[(size_t) i + 1]);
    const double d = a - 2.0 * b + c;
    return std::abs (d) < 1e-20 ? (double) i : i + 0.5 * (a - c) / d;
}

float energyDb (const juce::AudioBuffer<float>& b, int from, int len)
{
    double e = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < std::min (from + len, b.getNumSamples()); ++i) e += (double) b.getSample (c, i) * b.getSample (c, i);
    return (float) (10.0 * std::log10 (e / std::max (1, len) + 1e-30));
}

// The 30 ms from 1 ms before `at`, every channel: the octaves 250 Hz - 4 kHz hold 1 % or more of
// its energy, four of the five within 30 dB of the strongest. An impulse response is so; a chirp
// (a sweep's inverse run over something that is not the sweep) is narrow at any instant.
bool broadbandAt (const std::vector<std::vector<float>>& hc, int at, double sr)
{
    const int a = std::max (0, at - (int) (0.001 * sr)), len = (int) (0.031 * sr);
    const int order = orderFor (len), N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf (2 * (size_t) N);
    double band[5] {}, all = 0.0;
    for (const auto& h : hc)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < len && a + i < (int) h.size(); ++i)
            buf[(size_t) i] = h[(size_t) (a + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * kPiD * i / (len - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k <= N / 2; ++k) all += (double) buf[(size_t) k] * buf[(size_t) k];
        for (int b = 0; b < 5; ++b)
        {
            const double fc = 250.0 * (1 << b);
            for (int k = (int) std::ceil (fc / std::sqrt (2.0) * N / sr); k <= std::min (N / 2, (int) std::floor (fc * std::sqrt (2.0) * N / sr)); ++k)
                band[b] += (double) buf[(size_t) k] * buf[(size_t) k];
        }
    }
    const double top = *std::max_element (band, band + 5);
    double in = 0.0;
    int near = 0;
    for (double e : band) { in += e; if (e > top * 1.0e-3) ++near; }
    return top > 0.0 && in >= 0.01 * all && near >= 4;
}

// From 1 ms before the first arrival to where the tail meets the noise; high-passed at 20 Hz;
// scaled to kEnergy. peak: the strongest arrival; limitEnd: where a second event begins (or the end).
// asIs (an IR file): it ends after its last moment over the floor, whatever silence comes between
// (a recording's tail is cut where it stays down 300 ms), and only the last 10 ms fade out (a
// recording's last quarter does), so no arrival is cut or faded. limited: set when limitEnd is
// where it ends (the tail was still over the floor there).
bool finish (Capture& c, const juce::AudioBuffer<float>& srcIn, double sr, int peak, int limitEnd, bool asIs = false, bool* limited = nullptr)
{
    const int chs = srcIn.getNumChannels();
    // The first arrival: the earliest 0.5 ms, up to 300 ms before the peak, within 30 dB of the peak's
    // and 20 dB over the floor (the quietest tenth before the peak). A finished IR's direct sound
    // before a stronger reflection stays in; a capture's noise and talk before the clap stay out. An
    // IR file's: its first moment 20 dB over the floor, however long before the peak, however weak.
    const int w = std::max (8, (int) (0.0005 * sr));
    const float peakDb = energyDb (srcIn, std::max (0, peak - w / 2), w);
    float floorDb = -300.0f;
    {
        std::vector<float> before;
        for (int i = 0; i + w <= peak; i += w) before.push_back (energyDb (srcIn, i, w));
        if (! before.empty())
        {
            std::nth_element (before.begin(), before.begin() + (long) (before.size() / 10), before.end());
            floorDb = before[before.size() / 10];
        }
    }
    const float thr = asIs ? floorDb + 20.0f : std::max (peakDb - 30.0f, floorDb + 20.0f);
    int onset = peak;
    for (int i = asIs ? 0 : std::max (0, peak - (int) (0.3 * sr)); i <= peak; i += std::max (1, w / 4))
        if (energyDb (srcIn, i, w) >= thr) { onset = i; break; }
    // 1 ms before it, with silence put in front where the file starts sooner (the fade-in takes
    // that ms, never the arrival).
    const int lead = (int) (0.001 * sr), pad = std::max (0, lead - onset);
    juce::AudioBuffer<float> src (chs, srcIn.getNumSamples() + pad);
    src.clear();
    for (int ch = 0; ch < chs; ++ch) src.copyFrom (ch, pad, srcIn, ch, 0, srcIn.getNumSamples());
    onset += pad; peak += pad; limitEnd += pad;
    const int start = onset - lead;
    const int maxEnd = std::min ({ limitEnd, src.getNumSamples(), start + (int) (kMaxSeconds * sr) });
    if (maxEnd - start < (int) (0.02 * sr)) { c.error = "TOO SHORT"; return false; }

    // Tail: 10 ms energy hops. Where the last quarter is flat, it is the noise floor (its median);
    // where it is still falling (an IR file that already ends in silence), there is no floor to
    // cut at. Cut where the smoothed decay comes within 5 dB of the floor.
    const int hop = std::max (16, (int) (0.01 * sr));
    std::vector<float> env;
    for (int i = start; i + hop <= maxEnd; i += hop) env.push_back (energyDb (src, i, hop));
    if (env.size() < 3) { c.error = "TOO SHORT"; return false; }
    const int peakHop = (int) (std::max_element (env.begin(), env.end()) - env.begin());
    const size_t q0 = env.size() - std::max<size_t> (3, env.size() / 4);
    std::vector<float> lastQ (env.begin() + (long) q0, env.end());
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t k = 0; k < lastQ.size(); ++k) { const double x = (double) k * hop / sr; sx += x; sy += lastQ[k]; sxx += x * x; sxy += x * lastQ[k]; }
    const double nq = (double) lastQ.size();
    const double slope = nq * sxx - sx * sx > 1e-12 ? (nq * sxy - sx * sy) / (nq * sxx - sx * sx) : 0.0;   // dB per second
    std::nth_element (lastQ.begin(), lastQ.begin() + (long) lastQ.size() / 2, lastQ.end());
    float noise = lastQ[lastQ.size() / 2];
    if (env.size() < 20 || slope < -10.0) noise = *std::min_element (env.begin(), env.end()) - 10.0f;
    c.snrDb = env[(size_t) peakHop] - noise;
    // ... and stays there for 300 ms: a finished IR's later arrival after a silent gap is kept.
    auto smoothed = [&] (int h) { return 10.0f * std::log10 ((std::pow (10.0f, env[(size_t) h] / 10.0f) + std::pow (10.0f, env[(size_t) h + 1] / 10.0f) + std::pow (10.0f, env[(size_t) h + 2] / 10.0f)) / 3.0f); };
    const int hold = std::max (1, (int) (0.3 * sr / hop));
    int cut = (int) env.size();
    if (asIs)
    {
        // The last hop over the floor, and two more.
        int last = peakHop;
        for (int h = (int) env.size() - 1; h > peakHop; --h) if (env[(size_t) h] > noise + 5.0f) { last = h; break; }
        cut = std::min ((int) env.size(), last + 3);
    }
    else
        for (int h = peakHop + 1; h + 2 < (int) env.size(); ++h)
        {
            if (smoothed (h) > noise + 5.0f) continue;
            int back = -1;
            for (int k = h + 1; k < std::min (h + hold, (int) env.size() - 2) && back < 0; ++k) if (smoothed (k) > noise + 5.0f) back = k;
            if (back < 0) { cut = h + 2; break; }
            h = back;
        }
    const int end = std::min (maxEnd, start + cut * hop);
    const int len = end - start;
    if (limited != nullptr) *limited = cut == (int) env.size() && maxEnd == limitEnd && limitEnd < src.getNumSamples();   // not down in the floor first
    juce::AudioBuffer<float> out (chs, len);
    for (int ch = 0; ch < chs; ++ch) out.copyFrom (ch, 0, src, ch, start, len);
    // Fades: 1 ms in; out over the last quarter (10-400 ms), or the last 10 ms of an IR file.
    const int fin = std::max (1, (int) (0.001 * sr));
    const int fout = asIs ? std::min ((int) (0.01 * sr), std::max (1, len / 4)) : juce::jlimit ((int) (0.01 * sr), (int) (0.4 * sr), len / 4);
    for (int ch = 0; ch < chs; ++ch)
    {
        float* x = out.getWritePointer (ch);
        for (int i = 0; i < std::min (fin, len); ++i) x[i] *= (float) (0.5 - 0.5 * std::cos (kPiD * i / fin));
        for (int i = 0; i < std::min (fout, len); ++i) x[len - 1 - i] *= (float) (0.5 - 0.5 * std::cos (kPiD * i / fout));
        // 20 Hz high-pass (2nd order Butterworth): no DC, no wind rumble.
        const double wc = 2.0 * kPiD * 20.0 / sr, cs = std::cos (wc), al = std::sin (wc) / (2.0 * 0.70710678);
        const double a0 = 1.0 + al, b0 = (1.0 + cs) / 2.0 / a0, b1 = -(1.0 + cs) / a0, b2 = b0, a1 = -2.0 * cs / a0, a2 = (1.0 - al) / a0;
        double z1 = 0.0, z2 = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double in = x[i], y = b0 * in + z1;
            z1 = b1 * in - a1 * y + z2;
            z2 = b2 * in - a2 * y;
            x[i] = (float) y;
        }
    }
    // Something must be left (a click faded away, or samples that are not numbers, is no place).
    double e = 0.0;
    for (int ch = 0; ch < chs; ++ch)
        for (int i = 0; i < len; ++i) e += (double) out.getSample (ch, i) * out.getSample (ch, i);
    if (! std::isfinite (e) || e < 1.0e-20) { c.error = "SILENT"; return false; }
    c.rt60 = rt60 (out, sr);
    normalise (out);
    c.ir = std::move (out);
    c.sr = sr;
    c.ok = true;
    return true;
}
} // namespace

std::vector<float> sweep (double sr)
{
    const double T = kSweepSec, L = T / std::log (kF2 / kF1);
    const int n = (int) std::lround (T * sr);
    const int fadeIn = (int) (0.2 * sr), fadeOut = (int) (0.02 * sr);
    std::vector<float> s ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double v = std::sin (2.0 * kPiD * kF1 * L * (std::exp (t / L) - 1.0));
        // The lowest octaves come in softer, shaped like a speaker's own roll-off (2nd-order high-pass at
        // kSoftHz on the sweep's frequency at that moment: -19 dB at 20 Hz, -3 dB at 60 Hz, flat above
        // ~120 Hz). A small speaker isn't driven where it can't play; a big one still plays its lows.
        // The inverse is built from this same sweep, so the analysis takes the shape out again.
        const double r = kF1 * std::exp (t / L) / kSoftHz, r2 = r * r;
        v *= r2 / std::sqrt (1.0 + r2 * r2);
        if (i < fadeIn) v *= 0.5 - 0.5 * std::cos (kPiD * i / fadeIn);
        if (i >= n - fadeOut) v *= 0.5 - 0.5 * std::cos (kPiD * (n - 1 - i) / fadeOut);
        s[(size_t) i] = (float) v;
    }
    return s;
}

juce::AudioBuffer<float> sweepFile (double sr)
{
    const auto s = sweep (sr);
    const int lead = (int) std::lround (kLeadSec * sr), gap = (int) std::lround (kGapSec * sr), n = (int) s.size();
    juce::AudioBuffer<float> b (1, lead + kRepeats * (n + gap));
    b.clear();
    for (int r = 0; r < kRepeats; ++r)
        for (int i = 0; i < n; ++i) b.setSample (0, lead + r * (n + gap) + i, 0.5f * s[(size_t) i]);
    return b;
}

Capture fromSweep (const juce::AudioBuffer<float>& recIn, double sr, Source source)
{
    Capture c;
    c.sr = sr;
    if (sr < 44000.0) { c.error = "RECORD AT 44.1 KHZ OR MORE"; return c; }
    const int chs = std::min (2, recIn.getNumChannels());
    juce::AudioBuffer<float> rec (chs, recIn.getNumSamples());
    for (int ch = 0; ch < chs; ++ch) rec.copyFrom (ch, 0, recIn, ch, 0, recIn.getNumSamples());
    const int sweepN = (int) std::lround (kSweepSec * sr);
    const double period = (kSweepSec + kGapSec) * sr;
    const int order = orderFor ((int64_t) rec.getNumSamples() + sweepN + 1);
    auto inv = inverseSweep (sr, order);
    // Each channel deconvolved; the evidence is their energy together (channels of opposite polarity
    // do not cancel, as in a sum).
    std::vector<std::vector<float>> hc ((size_t) chs);
    std::vector<float> h;
    auto deconvolveAll = [&]
    {
        for (int ch = 0; ch < chs; ++ch) hc[(size_t) ch] = deconvolve (rec.getReadPointer (ch), rec.getNumSamples(), inv, order);
        h.assign (hc[0].size(), 0.0f);
        for (size_t i = 0; i < h.size(); ++i)
        {
            float e = 0.0f;
            for (int ch = 0; ch < chs; ++ch) e += hc[(size_t) ch][i] * hc[(size_t) ch][i];
            h[i] = std::sqrt (e / (float) chs);
        }
    };
    deconvolveAll();
    // Is there a sweep? Deconvolved, a sweep is a spike 40 dB or more over the median. Anything else
    // stays noise-like, or turns into a chirp, narrow at any instant: a click in silence does that,
    // with a median near zero. So for AUTO the spike must also be broadband from its first moment (an
    // impulse response), or come back at the sweep's spacing, which only the sweep file's repeats do:
    // a narrow speaker or room (little left of 250 Hz-4 kHz) is still a sweep. The performer's SWEEP
    // needs the spike only; SF CAPTURE's take, the spike and two of its repeats (it was played three
    // times, in step), or it failed.
    const int limit = rec.getNumSamples();
    std::vector<float> mag;
    for (int i = 0; i < limit; i += 7) mag.push_back (h[(size_t) i]);
    const bool known = source == Source::KnownSweep, asserted = source == Source::Sweep;
    const char* none = known ? "THE SWEEP DIDN'T COME BACK (THE SPEAKER, THE MICS, THE ROUTING?)" : "NO SWEEP";
    if (mag.empty()) { c.error = "EMPTY"; return c; }
    std::nth_element (mag.begin(), mag.begin() + (long) mag.size() / 2, mag.end());
    const float median = std::max (mag[mag.size() / 2], 1.0e-12f);
    int topAt = 0;
    for (int i = 0; i < limit; ++i) if (h[(size_t) i] > h[(size_t) topAt]) topAt = i;
    auto peaks = findPeaks (h, limit, 15.0f, (int) (0.6 * period));
    int repeats = 0;                                           // spikes at whole periods from the first (0.4 %: any clock drift accepted below)
    if (! peaks.empty())
        for (int p : peaks)
        {
            const double k = std::round ((p - peaks.front()) / period);
            if (std::abs ((p - peaks.front()) - k * period) <= 0.004 * std::max (1.0, k) * period) ++repeats;
        }
    const bool spike = 20.0f * std::log10 (h[(size_t) topAt] / median) >= 40.0f;
    const bool isSweep = spike && (asserted || (known ? repeats >= 2 : (repeats >= 2 || broadbandAt (hc, topAt, sr))));
    if (! isSweep) { c.error = none; return c; }
    double drift = 1.0;                                        // playback pace (1 = on the recorder's clock)
    // Drift: the playback device's clock against the recorder's, from the sweeps' spacing (a phone or
    // a Bluetooth speaker is often tens of ppm off; over a 10 s sweep that smears the highs).
    if (peaks.size() >= 2)
    {
        const double p0 = refine (h, peaks.front());
        double num = 0.0, den = 0.0;
        for (size_t i = 1; i < peaks.size(); ++i)
        {
            const double d = refine (h, peaks[i]) - p0;
            const double k = std::round (d / period);
            if (k >= 1.0) { num += d * k * period; den += k * period * k * period; }
        }
        const double r = den > 0.0 ? num / den : 1.0;            // measured / nominal
        c.driftPpm = (r - 1.0) * 1.0e6;
        // The sweep as it was played: the reference is generated at the drifted rate, so the room's
        // response comes out on the recorder's own clock, untouched.
        if (std::abs (r - 1.0) > 3.0e-6 && std::abs (r - 1.0) < 3.0e-3)
        {
            drift = r;
            inv = inverseSweep (sr * r, order);
            deconvolveAll();
            peaks = findPeaks (h, rec.getNumSamples(), 15.0f, (int) (0.6 * period));
        }
    }
    if (peaks.empty()) { c.error = none; return c; }
    // A channel the sweep isn't in (a mic unplugged, an input not routed) is left out: the place is
    // mono, from the other, and the status line says so. In a channel: its own spike, 40 dB over its
    // own median (a quiet channel, or a narrow one, still has it; silence and noise don't).
    std::vector<int> keep;
    for (int ch = 0; ch < chs; ++ch)
    {
        const auto& hx = hc[(size_t) ch];
        std::vector<float> m;
        for (int i = 0; i < limit; i += 7) m.push_back (std::fabs (hx[(size_t) i]));
        std::nth_element (m.begin(), m.begin() + (long) m.size() / 2, m.end());
        float top = 0.0f;
        for (int i = 0; i < limit; ++i) top = std::max (top, std::fabs (hx[(size_t) i]));
        if (top > 0.0f && 20.0f * std::log10 (top / std::max (m[m.size() / 2], 1.0e-12f)) >= 40.0f) keep.push_back (ch);
    }
    if (keep.empty()) { c.error = none; return c; }
    if ((int) keep.size() < chs)
        c.note = juce::String ("NO SWEEP IN THE ") + (keep[0] == 0 ? "RIGHT" : "LEFT") + " CHANNEL: MONO, FROM THE " + (keep[0] == 0 ? "LEFT" : "RIGHT");
    // Each channel's response, averaged over every sweep that has its whole window.
    // Where each response starts: the first sweep's peak, then whole periods at the playback's
    // own pace (fractional), so the responses add up sample-exact. The window opens up to 300 ms
    // before the peak (the strongest arrival; an earlier, weaker direct sound is kept by finish).
    const int p0 = peaks.front();
    const int pre = std::min ((int) (0.3 * sr), p0);
    const int maxLen = (int) (std::min (period - 2.6 * sr, kMaxSeconds * sr));
    std::vector<double> use;
    int len = maxLen;
    for (int p : peaks)
    {
        const double start = p0 - pre + std::round ((p - p0) / (period * drift)) * period * drift;
        if (start >= 0.0 && rec.getNumSamples() - p > (int) (0.25 * sr)) { use.push_back (start); len = std::min (len, rec.getNumSamples() - (int) start - pre); }
    }
    if (use.empty()) { c.error = "SWEEP CUT OFF"; return c; }
    juce::AudioBuffer<float> avg ((int) keep.size(), pre + len);
    avg.clear();
    for (int k = 0; k < (int) keep.size(); ++k)
    {
        const auto& hx = hc[(size_t) keep[(size_t) k]];
        for (double st : use)
        {
            const int ist = (int) std::floor (st);
            const double frac = st - ist;
            for (int i = 0; i < pre + len; ++i)
                avg.addSample (k, i, (frac < 1e-6 ? hx[(size_t) (ist + i)] : readFrac (hx, ist + i + frac)) / (float) use.size());
        }
    }
    // The sweep is at -6 dBFS in the file: the level is set by normalise() anyway.
    if (! finish (c, avg, sr, pre, avg.getNumSamples())) return c;
    c.kind = "SWEEP x" + juce::String ((int) use.size());
    return c;
}

Capture fromImpulse (const juce::AudioBuffer<float>& rec, double sr, bool asIs)
{
    Capture c;
    c.sr = sr;
    const int chs = std::min (2, rec.getNumChannels());
    if (rec.getNumSamples() < 1 || chs < 1) { c.error = "TOO SHORT"; return c; }
    // A file shorter than 50 ms (a finished IR can be a few ms) goes on in silence.
    const int n = std::max (rec.getNumSamples(), (int) (0.05 * sr));
    juce::AudioBuffer<float> src (chs, n);
    src.clear();
    for (int ch = 0; ch < chs; ++ch) src.copyFrom (ch, 0, rec, ch, 0, rec.getNumSamples());
    int peak = 0;
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float e = 0.0f;
        for (int ch = 0; ch < chs; ++ch) e += src.getSample (ch, i) * src.getSample (ch, i);
        if (e > pk) { pk = e; peak = i; }
    }
    if (pk < 1.0e-10f) { c.error = "SILENT"; return c; }
    // A clap recording: a second event (another clap) is where the 10 ms energy jumps 12 dB over its
    // lowest point since the first, 30 ms or more after it, to within 6 dB of the first. The IR stops
    // just before, and the status line says so. An IR file is kept whole: a strong reflection is
    // part of the place (it used to be taken for a second clap, and cut).
    const int hop = std::max (16, (int) (0.01 * sr));
    int limitEnd = n;
    if (! asIs)
    {
        float lowest = 1.0e9f;
        const float firstDb = energyDb (src, std::max (0, peak - hop / 2), hop);
        for (int i = peak + (int) (0.03 * sr); i + hop <= n; i += hop)
        {
            const float e = energyDb (src, i, hop);
            if (e > lowest + 12.0f && e > firstDb - 6.0f) { limitEnd = std::max (peak + hop, i - hop / 2); break; }
            lowest = std::min (lowest, e);
        }
    }
    bool limited = false;
    if (! finish (c, src, sr, peak, limitEnd, asIs, &limited)) return c;
    c.kind = asIs ? "IR FILE" : "CLAP";
    if (limited) c.note = "CUT AT " + juce::String ((double) limitEnd / sr, 2) + " S: A SECOND CLAP";   // its tail was still over the noise
    return c;
}

namespace
{
// AUTO: an IR file has nothing before its first arrival (edited to start there: under 5 ms of
// file before it, or digital silence, under -95 dBFS, a 16-bit file's dither included); a recording
// has its room's noise there. The first arrival: the first sample over 1 % of the peak and 20 dB
// over the file's quietest 50 ms, so that a noisy room's noise is not taken for it.
bool looksFinished (const juce::AudioBuffer<float>& rec, double sr)
{
    const int chs = std::min (2, rec.getNumChannels()), n = rec.getNumSamples();
    float pk = 0.0f;
    for (int ch = 0; ch < chs; ++ch) pk = std::max (pk, rec.getMagnitude (ch, 0, n));
    if (pk <= 0.0f) return true;
    // The floor: the quietest 50 ms (5 hops of 10 ms; a short file, a quarter of it).
    const int hop = std::max (1, std::min ((int) (0.01 * sr), n / 20)), hops = n / hop;
    std::vector<double> e ((size_t) hops, 0.0);
    for (int h = 0; h < hops; ++h)
        for (int ch = 0; ch < chs; ++ch)
            for (int i = h * hop; i < (h + 1) * hop; ++i) e[(size_t) h] += (double) rec.getSample (ch, i) * rec.getSample (ch, i);
    double quiet = 1.0e30;
    for (int h = 0; h + 5 <= hops; ++h) quiet = std::min (quiet, e[(size_t) h] + e[(size_t) h + 1] + e[(size_t) h + 2] + e[(size_t) h + 3] + e[(size_t) h + 4]);
    const float floorRms = hops >= 5 ? (float) std::sqrt (quiet / (5.0 * hop * chs)) : 0.0f;
    const float over = std::max (0.01f * pk, 10.0f * floorRms);
    int first = n;
    for (int ch = 0; ch < chs; ++ch)
        for (int i = 0; i < n; ++i) if (std::fabs (rec.getSample (ch, i)) > over) { first = std::min (first, i); break; }
    const int before = first - (int) (0.001 * sr);
    if (before < (int) (0.005 * sr)) return true;
    double sum = 0.0;
    for (int ch = 0; ch < chs; ++ch) for (int i = 0; i < before; ++i) sum += (double) rec.getSample (ch, i) * rec.getSample (ch, i);
    return 10.0 * std::log10 (sum / ((double) before * chs) + 1e-30) <= -95.0;
}
} // namespace

Capture analyse (const juce::AudioBuffer<float>& rec, double sr, Source source)
{
    if (rec.getNumSamples() > (int) (kMaxRecording * sr))
    {
        Capture c;
        c.error = "OVER 90 S: TRIM IT";
        return c;
    }
    for (int ch = 0; ch < rec.getNumChannels(); ++ch)
        for (int i = 0; i < rec.getNumSamples(); ++i)
            if (! std::isfinite (rec.getSample (ch, i))) { Capture c; c.error = "SAMPLES THAT ARE NOT NUMBERS"; return c; }
    // A sweep recording is at least one sweep long (and made at 44.1 kHz or more: the sweep goes to 20 kHz).
    const bool sweepLong = rec.getNumSamples() >= (int) (kSweepSec * sr) && sr >= 44000.0;
    switch (source)
    {
        case Source::IrFile: return fromImpulse (rec, sr, true);
        case Source::Clap:   return fromImpulse (rec, sr, false);
        case Source::Sweep:
        case Source::KnownSweep:
        {
            if (! sweepLong) { Capture c; c.error = sr < 44000.0 ? "RECORD AT 44.1 KHZ OR MORE" : "SHORTER THAN ONE SWEEP"; return c; }
            return fromSweep (rec, sr, source);       // never an impulse: a sweep that didn't come back fails
        }
        case Source::Auto: break;
    }
    if (sweepLong)
    {
        auto s = fromSweep (rec, sr, Source::Auto);
        if (s.ok || s.error != "NO SWEEP") return s;
    }
    return fromImpulse (rec, sr, looksFinished (rec, sr));
}

float rt60 (const juce::AudioBuffer<float>& b, double sr)
{
    const int n = b.getNumSamples();
    if (n < 16) return 0.0f;
    std::vector<double> e ((size_t) n);
    double acc = 0.0;
    for (int i = n - 1; i >= 0; --i)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch) acc += (double) b.getSample (ch, i) * b.getSample (ch, i);
        e[(size_t) i] = acc;
    }
    if (acc <= 0.0) return 0.0f;
    auto db = [&] (int i) { return 10.0 * std::log10 (e[(size_t) i] / acc + 1e-30); };
    int i5 = -1, iEnd = -1;
    for (int i = 0; i < n; ++i) if (db (i) <= -5.0) { i5 = i; break; }
    for (int i = std::max (0, i5); i < n && i5 >= 0; ++i) if (db (i) <= -25.0) { iEnd = i; break; }
    if (iEnd < 0) for (int i = std::max (0, i5); i < n && i5 >= 0; ++i) if (db (i) <= -15.0) { iEnd = i; break; }
    if (i5 < 0 || iEnd <= i5 + 2) return (float) (n / sr);
    // Least squares over the range.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int cnt = 0;
    const int step = std::max (1, (iEnd - i5) / 400);
    for (int i = i5; i <= iEnd; i += step)
    {
        const double x = i / sr, y = db (i);
        sx += x; sy += y; sxx += x * x; sxy += x * y; ++cnt;
    }
    const double slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
    return slope < -1e-3 ? (float) (-60.0 / slope) : (float) (n / sr);
}

void decorrelate (const float* in, float* out, int n, double sr)
{
    // Velvet noise: one impulse of random sign in every 1 ms, decaying over 12 ms, 30 ms long.
    std::mt19937 rng (1234567u);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    const int grid = std::max (1, (int) (0.001 * sr)), count = 30;
    std::vector<std::pair<int, float>> vn;
    double e = 0.0;
    for (int k = 0; k < count; ++k)
    {
        const int pos = k * grid + (int) (u (rng) * (grid - 1));
        const float g = (float) ((u (rng) < 0.5 ? -1.0 : 1.0) * std::exp (-(double) pos / (0.012 * sr)));
        vn.push_back ({ pos, g });
        e += (double) g * g;
    }
    for (auto& p : vn) p.second /= (float) std::sqrt (e);
    const int a = (int) (0.005 * sr), b = (int) (0.015 * sr);
    for (int i = 0; i < n; ++i)
    {
        double y = 0.0;
        for (const auto& p : vn) if (i - p.first >= 0) y += p.second * in[i - p.first];
        const double w = i <= a ? 0.0 : i >= b ? 1.0 : 0.5 - 0.5 * std::cos (kPiD * (i - a) / (b - a));
        out[i] = (float) (in[i] + w * (y - in[i]));
    }
}

std::vector<float> stretch (const float* in, int n, double ratio)
{
    const int outN = std::max (1, (int) std::lround (n * ratio));
    std::vector<float> out ((size_t) outN, 0.0f);
    if (std::abs (ratio - 1.0) < 1e-12) { std::copy (in, in + std::min (n, outN), out.begin()); return out; }
    const auto& table = sincTable();
    const int res = kRes;
    const double c = 0.475 * std::min (1.0, ratio);             // cycles per input sample
    const double T = kZeros / (2.0 * c);                        // half-length in input samples
    const double scale = 2.0 * c;
    for (int i = 0; i < outN; ++i)
    {
        const double x = i / ratio;
        const int lo = std::max (0, (int) std::ceil (x - T)), hi = std::min (n - 1, (int) std::floor (x + T));
        double acc = 0.0;
        for (int k = lo; k <= hi; ++k)
        {
            const double uu = std::fabs ((x - k) * scale) * res;
            const int ui = (int) uu;
            if (ui >= (int) table.size() - 1) continue;
            const double kern = table[(size_t) ui] + (uu - ui) * (table[(size_t) ui + 1] - table[(size_t) ui]);
            acc += kern * in[k];
        }
        out[(size_t) i] = (float) (acc * scale);
    }
    return out;
}

void darken (float* x, int n, double sr, float octPerSec)
{
    if (octPerSec <= 0.0f) return;
    Svf f;
    const float top = (float) std::min (20000.0, 0.45 * sr);
    for (int i = 0; i < n; ++i)
    {
        if ((i & 31) == 0)
        {
            const float hz = std::max (150.0f, top * std::pow (2.0f, -octPerSec * (float) (i / sr)));
            f.set (hz, 0.6f, sr);
        }
        x[i] = f.tickLP (x[i]);
    }
}

juce::AudioBuffer<float> builtInRoom (double sr)
{
    const int n = (int) (1.8 * sr);
    const float rt = 1.3f;
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        std::mt19937 rng (991u + (unsigned) ch * 7919u);
        std::normal_distribution<float> g (0.0f, 1.0f);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        float* x = b.getWritePointer (ch);
        // Early reflections: 14 between 3 and 48 ms.
        for (int k = 0; k < 14; ++k)
        {
            const float t = 0.003f + 0.045f * u (rng);
            const int i = (int) (t * (float) sr);
            x[i] += (u (rng) < 0.5f ? -1.0f : 1.0f) * (0.25f + 0.35f * u (rng)) * std::exp (-t / 0.1f);
        }
        // The diffuse tail from 8 ms, fading in to 30 ms.
        const int a = (int) (0.008 * sr), z = (int) (0.03 * sr);
        for (int i = a; i < n; ++i)
        {
            const float t = (float) (i / sr);
            const float fade = i >= z ? 1.0f : (float) (i - a) / (float) (z - a);
            x[i] += g (rng) * 0.08f * fade * std::exp (-6.9078f * t / rt);
        }
        darken (x, n, sr, 2.5f);
        for (int i = 0; i < (int) (0.3 * sr); ++i) x[n - 1 - i] *= (float) (0.5 - 0.5 * std::cos (kPiD * i / (0.3 * sr)));
    }
    normalise (b);
    return b;
}

void normalise (juce::AudioBuffer<float>& b)
{
    double e = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    e /= std::max (1, b.getNumChannels());
    if (e > 0.0) b.applyGain ((float) std::sqrt (kEnergy / e));
}

std::vector<int> pickTracks (const juce::StringArray& names, int numChannels)
{
    std::vector<int> picks;
    if (names.size() == numChannels)
    {
        for (int i = 0; i < numChannels && picks.size() < 2; ++i)
            if (! names[i].trim().startsWithIgnoreCase ("mix")) picks.push_back (i);
        if (! picks.empty()) return picks;
    }
    for (int i = 0; i < std::min (2, numChannels); ++i) picks.push_back (i);
    return picks;
}

namespace
{
// The iXML chunk of a RIFF / RF64 WAV file, or empty.
juce::String ixmlOf (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return {};
    char id[4] = {};
    auto is = [&] (const char* s) { return std::memcmp (id, s, 4) == 0; };
    if (in.read (id, 4) != 4 || ! (is ("RIFF") || is ("RF64") || is ("BW64"))) return {};
    (void) in.readInt();
    if (in.read (id, 4) != 4 || ! is ("WAVE")) return {};
    juce::int64 dataSize = -1;
    while (in.read (id, 4) == 4)
    {
        const auto size32 = (juce::uint32) in.readInt();
        auto size = (juce::int64) size32;
        const auto at = in.getPosition();
        if (is ("ds64") && size >= 16) { (void) in.readInt64(); dataSize = in.readInt64(); }
        else if (is ("data") && size32 == 0xffffffffu) { if (dataSize < 0) return {}; size = dataSize; }   // RF64: the size is in ds64
        else if (is ("iXML"))
        {
            if (size <= 0 || size > (1 << 20)) return {};
            juce::MemoryBlock mb;
            if (in.readIntoMemoryBlock (mb, (int) size) != (size_t) size) return {};
            return mb.toString();
        }
        if (! in.setPosition (at + size + (size & 1))) break;
    }
    return {};
}
} // namespace

juce::StringArray trackNames (const juce::File& wav, const juce::String& bextDescription, int numChannels)
{
    if (numChannels <= 0) return {};
    juce::StringArray names;
    for (int i = 0; i < numChannels; ++i) names.add ({});
    bool any = false;
    if (const auto xml = juce::parseXML (ixmlOf (wav)))
        if (const auto* list = xml->getChildByName ("TRACK_LIST"))
        {
            int order = 0;
            for (const auto* t : list->getChildWithTagNameIterator ("TRACK"))
            {
                ++order;
                int idx = t->getChildElementAllSubText ("INTERLEAVE_INDEX", {}).trim().getIntValue();   // 1-based place in the file
                if (idx <= 0) idx = order;
                const auto name = t->getChildElementAllSubText ("NAME", {}).trim();
                if (idx <= numChannels && name.isNotEmpty()) { names.set (idx - 1, name); any = true; }
            }
        }
    if (! any)
        for (auto line : juce::StringArray::fromLines (bextDescription))
        {
            // "sTRK3=Ch1": one letter (s Sound Devices, z Zoom), TRK, the track's place in the file.
            line = line.trim();
            const int t = line.indexOf ("TRK"), eq = line.indexOfChar ('=');
            if (t < 0 || t > 1 || eq < t + 4) continue;
            const auto num = line.substring (t + 3, eq);
            if (! num.containsOnly ("0123456789")) continue;
            const int idx = num.getIntValue();
            const auto name = line.substring (eq + 1).trim();
            if (idx >= 1 && idx <= numChannels && name.isNotEmpty()) { names.set (idx - 1, name); any = true; }
        }
    return any ? names : juce::StringArray();
}

bool sameChannels (const juce::AudioBuffer<float>& b)
{
    if (b.getNumChannels() != 2) return false;
    const float* l = b.getReadPointer (0);
    const float* r = b.getReadPointer (1);
    double d = 0.0, s = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double x = l[i], y = r[i];
        d += (x - y) * (x - y);
        s += x * x + y * y;
    }
    return s > 0.0 && d <= 1.0e-6 * s;
}

Capture joinTakes (const Capture& left, const Capture& right)
{
    Capture c;
    if (! left.ok || ! right.ok) { c.error = ! left.ok ? left.error : right.error; return c; }
    const int nl = left.ir.getNumSamples(), nr = right.ir.getNumSamples();
    c.ir.setSize (2, std::max (nl, nr));
    c.ir.clear();
    c.ir.copyFrom (0, 0, left.ir, 0, 0, nl);
    c.ir.copyFrom (1, 0, right.ir, 0, 0, nr);
    c.ok = true;
    c.sr = left.sr;
    c.kind = left.kind == right.kind ? left.kind + " (2 TAKES)" : left.kind + " / " + right.kind;
    if (left.note.isNotEmpty() || right.note.isNotEmpty())
        c.note = (left.note.isNotEmpty() ? "L " + left.note : juce::String()) + (left.note.isNotEmpty() && right.note.isNotEmpty() ? "; " : "")
               + (right.note.isNotEmpty() ? "R " + right.note : juce::String());
    c.snrDb = std::min (left.snrDb, right.snrDb);
    c.rt60 = 0.5f * (left.rt60 + right.rt60);
    c.driftPpm = std::abs (left.driftPpm) > std::abs (right.driftPpm) ? left.driftPpm : right.driftPpm;
    return c;
}
} // namespace ir
} // namespace sf
