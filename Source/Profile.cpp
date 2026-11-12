#include "Profile.h"
#include <juce_dsp/juce_dsp.h>

namespace sf0
{
static float lin (double x, double a, double b) { return (float) juce::jlimit (0.0, 1.0, (x - a) / (b - a)); }
static double dB (double p) { return 10.0 * std::log10 (std::max (p, 1.0e-20)); }

Profile profileOf (const Engine::Heard& h)
{
    Profile p;
    const int len = h.audio.getNumSamples();
    const double sr = h.sampleRate;
    constexpr int order = 11, N = 1 << order, hop = 512, bins = N / 2;
    if (len < N * 4 || sr < 8000.0) return p;

    std::vector<float> mono ((size_t) len), win ((size_t) N), buf ((size_t) (2 * N));
    {
        const float* L = h.audio.getReadPointer (0);
        const float* R = h.audio.getReadPointer (h.audio.getNumChannels() > 1 ? 1 : 0);
        for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (L[i] + R[i]);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    }
    juce::dsp::FFT fft (order);
    const int frames = (len - N) / hop + 1;
    const double hz = sr / (double) N;
    auto bin = [hz] (double f) { return juce::jlimit (1, bins - 1, (int) std::lround (f / hz)); };
    int edge[17];
    for (int k = 0; k <= 16; ++k)
    {
        edge[k] = bin (30.0 * std::pow (16000.0 / 30.0, (double) k / 16.0));
        if (k > 0) edge[k] = std::min (bins, std::max (edge[k], edge[k - 1] + 1));
    }
    const int bLow = bin (200.0), bTop = bin (2000.0), bMid0 = bin (200.0), bMid1 = bin (5000.0);

    std::vector<double> avg ((size_t) bins, 0.0), midGeo ((size_t) frames), midAri ((size_t) frames);
    std::vector<float> prev ((size_t) bins, 0.0f), prevLow ((size_t) bins, 0.0f), fluxAll ((size_t) frames), fluxLow ((size_t) frames), fluxTop ((size_t) frames);
    std::vector<std::array<double, 16>> bandPow ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = mono[(size_t) (f * hop + i)] * win[(size_t) i];
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double fa = 0.0, fl = 0.0, ft = 0.0, geo = 0.0, ari = 0.0;
        for (int b = 1; b < bins; ++b)
        {
            const float m = buf[(size_t) b];
            const double pw = (double) m * m;
            avg[(size_t) b] += pw;
            const float lg = std::log1p (m);                // a hit shows as a rise, loud or soft
            const float d = lg - prev[(size_t) b];
            prev[(size_t) b] = lg;
            if (d > 0.0f && f > 0) { fa += d; if (b >= bTop) ft += d; }
            // (the low end by how much it rises, not by how many times over: a hat's click reaches
            // down here too, at a thousandth of a kick's size)
            if (b < bLow) { if (f > 0 && m > prevLow[(size_t) b]) fl += m - prevLow[(size_t) b]; prevLow[(size_t) b] = m; }
            if (b >= bMid0 && b < bMid1) { geo += std::log (pw + 1.0e-9); ari += pw + 1.0e-9; }
        }
        // (the low end is a handful of bins and the top hundreds: each as its mean rise, so they compare)
        fluxAll[(size_t) f] = (float) fa; fluxLow[(size_t) f] = (float) (fl / (double) (bLow - 1)); fluxTop[(size_t) f] = (float) (ft / (double) (bins - bTop));
        midGeo[(size_t) f] = geo / (double) (bMid1 - bMid0); midAri[(size_t) f] = ari / (double) (bMid1 - bMid0);
        for (int k = 0; k < 16; ++k)
        {
            double q = 0.0;
            for (int b = edge[k]; b < edge[k + 1]; ++b) q += (double) buf[(size_t) b] * buf[(size_t) b];
            bandPow[(size_t) f][(size_t) k] = q;
        }
    }

    // the spectrum's shape, and what share of it the low end and the top have
    double band[16] = {}, all = 0.0, low = 0.0, overBass = 0.0, top = 0.0, most = 0.0;
    for (int b = edge[0]; b < bins; ++b)
    {
        const double q = avg[(size_t) b];
        const double f = (double) b * hz;
        all += q;
        if (f < 120.0) low += q;
        if (f > 150.0) overBass += q;
        if (f > 2500.0) top += q;
    }
    for (int k = 0; k < 16; ++k) { for (int b = edge[k]; b < edge[k + 1]; ++b) band[k] += avg[(size_t) b]; most = std::max (most, band[k]); }
    if (! (all > 0.0)) return p;
    for (int k = 0; k < 16; ++k) p.bands[(size_t) k] = lin (dB (band[k] / most), -48.0, 0.0);
    // (the scales are set from this rig: a full mix has the low end 2 to 4 dB under the whole, a kick
    // filtered out of a build 12; the top is 27 dB under the rest for kick and bass alone, 5 for a
    // bright full mix, level with it for hats alone)
    p.weight = lin (dB (low / all), -22.0, -1.5);
    p.bright = lin (dB (top / std::max (overBass, 1.0e-20)), -26.0, -3.0);

    // hits: rises that stand above what is round them
    const double beats = h.bars > 0 ? h.bars * 4.0 : (double) len / sr * 2.2;
    {
        double mean = 0.0;
        for (auto v : fluxAll) mean += v;
        mean /= (double) frames;
        int hits = 0;
        for (int f = 1; f < frames - 1; ++f)
        {
            const float v = fluxAll[(size_t) f];
            bool peak = true;
            double round = 0.0; int n = 0;
            for (int g = std::max (0, f - 8); g <= std::min (frames - 1, f + 8); ++g)
            {
                round += fluxAll[(size_t) g]; ++n;
                if (std::abs (g - f) <= 3 && fluxAll[(size_t) g] > v) peak = false;
            }
            if (peak && v > 1.4 * round / (double) n + 0.25 * mean) ++hits;
        }
        p.density = lin ((double) hits / beats, 0.5, 5.0);
    }

    // tone against texture: how far the middle of the spectrum is from noise, in the frames that have any
    {
        double loudest = 0.0, sum = 0.0; int n = 0;
        for (auto a : midAri) loudest = std::max (loudest, a);
        for (int f = 0; f < frames; ++f)
            if (midAri[(size_t) f] > loudest * 1.0e-3) { sum += 10.0 * (midGeo[(size_t) f] / std::log (10.0)) - dB (midAri[(size_t) f]); ++n; }
        p.tone = n > 0 ? lin (sum / (double) n, -8.0, -28.0) : 0.0f;
    }

    if (h.bars > 0)
    {
        // where the hits fall in the bar. A rise shows a quarter of a window before the hit's own place.
        const double step = (double) len / (double) (h.bars * 16);
        double lowAt[16] = {}, topAt[16] = {}, lowPeak = 0.0, topPeak = 0.0;
        for (int f = 0; f < frames; ++f)
        {
            const double at = ((double) (f * hop) + 0.75 * N) / step;
            const double near = std::round (at);
            if (std::abs (at - near) > 0.4) continue;
            const int s = ((int) near % 16 + 16) % 16;
            lowAt[s] += fluxLow[(size_t) f]; topAt[s] += fluxTop[(size_t) f];
        }
        for (int s = 0; s < 16; ++s) { lowPeak = std::max (lowPeak, lowAt[s]); topPeak = std::max (topPeak, topAt[s]); }
        // each against its own strongest step, unless it is next to nothing beside the other
        // The top against its own strongest step. The low end against its strongest too, unless
        // that is next to nothing (a kick's is 60 to 130 a bar on this scale, a pad's 3, a hat's 0.2).
        const double lowRef = std::max (lowPeak, 20.0 * h.bars), topRef = std::max (topPeak, 1.0e-9);
        if (lowRef > 0.0)
            for (int s = 0; s < 16; ++s) { p.pulse[(size_t) s] = (float) (lowAt[s] / lowRef); p.pulse[(size_t) (16 + s)] = (float) (topAt[s] / topRef); }

        // movement: how far each band is from where it was a bar before
        if (h.bars >= 2)
        {
            const int perBar = frames / h.bars;
            double change = 0.0; int n = 0;
            std::array<double, 16> was {};
            for (int b = 0; b < h.bars; ++b)
            {
                std::array<double, 16> now {};
                double loud = 0.0;
                for (int f = b * perBar; f < (b + 1) * perBar; ++f) for (int k = 0; k < 16; ++k) now[(size_t) k] += bandPow[(size_t) f][(size_t) k];
                for (auto v : now) loud = std::max (loud, v);
                if (b > 0)
                    for (int k = 0; k < 16; ++k)
                        if (now[(size_t) k] > loud * 1.0e-4 && was[(size_t) k] > 0.0) { change += std::abs (dB (now[(size_t) k]) - dB (was[(size_t) k])); ++n; }
                was = now;
            }
            p.movement = n > 0 ? lin (change / (double) n, 0.2, 3.0) : 0.0f;
        }

        // from the rig: each lane's share, the returns', and the level of it all before the master
        for (int l = 0; l < kLanes; ++l) p.lanes[(size_t) l] = lin (h.laneDb[(size_t) l], -60.0, -22.0);
        const double wetPow = std::pow (10.0, h.retDb[0] / 10.0) + std::pow (10.0, h.retDb[1] / 10.0);
        p.wet = lin (dB (wetPow) - (double) h.mixDb, -30.0, -4.0);
    }
    // (with no bars to go by the level is the clip's own, which the limiter has evened out)
    const double level = h.bars > 0 ? lin (h.mixDb, -40.0, -14.0) : lin (dB (all / ((double) frames * (double) N * (double) N * 0.1875)), -40.0, -10.0);
    p.energy = (float) juce::jlimit (0.0, 1.0, 0.55 * level + 0.30 * p.density + 0.15 * p.bright);
    p.bpm = (float) h.bpm;
    p.bars = h.bars;
    p.has = true;
    return p;
}

float distance (const Profile& a, const Profile& b, bool colourOnly)
{
    auto sq = [] (float x, float y) { return (x - y) * (x - y); };
    auto mean = [&sq] (const float* x, const float* y, int n) { float s = 0.0f; for (int i = 0; i < n; ++i) s += sq (x[i], y[i]); return s / (float) n; };
    float d = sq (a.bright, b.bright) + 0.7f * sq (a.weight, b.weight) + 0.7f * sq (a.density, b.density) + 0.5f * sq (a.tone, b.tone) + 0.5f * sq (a.wet, b.wet)
            + mean (a.bands.data(), b.bands.data(), 16) + 0.5f * mean (a.pulse.data(), b.pulse.data(), 32) + 0.7f * mean (a.lanes.data(), b.lanes.data(), kLanes);
    if (! colourOnly) d += sq (a.energy, b.energy);
    return d;
}

// ------------------------------------------------------------------------------------- keeping it
static juce::var bytes (const float* v, int n)
{
    juce::Array<juce::var> a;
    for (int i = 0; i < n; ++i) a.add (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v[i]) * 255.0f));
    return a;
}

static void unbytes (const juce::var& v, float* into, int n)
{
    if (auto* a = v.getArray()) for (int i = 0; i < n && i < a->size(); ++i) into[i] = (float) (int) (*a)[i] / 255.0f;
}

juce::var Profile::toVar() const
{
    auto* o = new juce::DynamicObject();
    auto set = [o] (const char* k, float v) { o->setProperty (k, juce::roundToInt (v * 1000.0f) / 1000.0); };
    o->setProperty ("v", kProfileVersion);
    set ("energy", energy); set ("weight", weight); set ("bright", bright); set ("density", density);
    set ("tone", tone); set ("movement", movement); set ("wet", wet); set ("bpm", bpm);
    o->setProperty ("bars", bars);
    if (! steady) o->setProperty ("steady", false);
    o->setProperty ("bands", bytes (bands.data(), (int) bands.size()));
    o->setProperty ("pulse", bytes (pulse.data(), (int) pulse.size()));
    o->setProperty ("lanes", bytes (lanes.data(), (int) lanes.size()));
    return juce::var (o);
}

Profile Profile::fromVar (const juce::var& v)
{
    Profile p;
    if (! v.isObject()) return p;
    auto get = [&v] (const char* k) { return (float) (double) v.getProperty (k, 0.0); };
    p.energy = get ("energy"); p.weight = get ("weight"); p.bright = get ("bright"); p.density = get ("density");
    p.tone = get ("tone"); p.movement = get ("movement"); p.wet = get ("wet"); p.bpm = get ("bpm");
    p.bars = (int) v.getProperty ("bars", 0);
    p.steady = (bool) v.getProperty ("steady", true);
    unbytes (v.getProperty ("bands", {}), p.bands.data(), (int) p.bands.size());
    unbytes (v.getProperty ("pulse", {}), p.pulse.data(), (int) p.pulse.size());
    unbytes (v.getProperty ("lanes", {}), p.lanes.data(), (int) p.lanes.size());
    p.has = true;
    return p;
}

juce::Colour heat (float bright)
{
    static const juce::Colour stops[] = { juce::Colour (0xff6E2A2B), juce::Colour (0xffB54142), juce::Colour (0xffC5843F), juce::Colour (0xffD7C352), juce::Colour (0xffE8E5E1) };
    const float x = juce::jlimit (0.0f, 1.0f, bright) * 4.0f;
    const int i = juce::jmin (3, (int) x);
    return stops[i].interpolatedWith (stops[i + 1], x - (float) i);
}
} // namespace sf0
