#include "Sample.h"
#include "Resample.h"
#include <algorithm>
#include <cmath>

namespace sf
{
static juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager fm;
    static bool init = false;
    if (! init) { fm.registerBasicFormats(); init = true; }
    return fm;
}

static void analyse (SampleData& s)
{
    const int n = s.buf.getNumSamples();
    if (n == 0) return;
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = 0.5f * (s.buf.getSample (0, i) + s.buf.getSample (s.buf.getNumChannels() > 1 ? 1 : 0, i));

    // Peak and decay (-40 dB after peak).
    int peakIdx = 0; float pk = 0.0f;
    for (int i = 0; i < n; ++i) if (std::fabs (mono[(size_t) i]) > pk) { pk = std::fabs (mono[(size_t) i]); peakIdx = i; }
    s.peak = std::max (pk, 1.0e-6f);
    int last = peakIdx;
    for (int i = peakIdx; i < n; ++i) if (std::fabs (mono[(size_t) i]) > pk * 0.01f) last = i;
    s.decaySec = (float) ((last - peakIdx) / s.sr);

    // Onsets: energy flux in 10 ms hops.
    const int hop = std::max (64, (int) (s.sr * 0.01));
    std::vector<float> e;
    for (int i = 0; i + hop <= n; i += hop)
    {
        float sum = 0.0f;
        for (int k = 0; k < hop; ++k) sum += mono[(size_t) (i + k)] * mono[(size_t) (i + k)];
        e.push_back (std::sqrt (sum / (float) hop));
    }
    float mean = 0.0f;
    for (auto v : e) mean += v;
    mean /= (float) std::max<size_t> (1, e.size());
    s.slices.clear();
    int lastOnset = -1000;
    for (size_t h = 1; h < e.size(); ++h)
    {
        const float flux = e[h] - e[h - 1];
        if (flux > mean * 0.6f && e[h] > mean * 0.5f && (int) h - lastOnset > 8)
        {
            s.slices.push_back ((int) h * hop);
            lastOnset = (int) h;
        }
    }
    if (s.slices.empty() || s.slices.front() > hop * 4) s.slices.insert (s.slices.begin(), 0);
    if (s.slices.size() > 32) s.slices.resize (32);

    // Pitch: normalised autocorrelation on a window after the attack (YIN-lite).
    const int start = std::min (n - 1, peakIdx + (int) (s.sr * 0.02));
    const int win = std::min ((int) (s.sr * 0.08), n - start);
    s.rootHz = 0.0f; s.rootNote = 60.0f;
    if (win > (int) (s.sr * 0.03))
    {
        const int minLag = (int) (s.sr / 1200.0), maxLag = std::min (win / 2, (int) (s.sr / 30.0));
        float best = 0.0f; int bestLag = 0;
        for (int lag = minLag; lag < maxLag; ++lag)
        {
            double num = 0, d1 = 0, d2 = 0;
            for (int i = 0; i < win - lag; ++i)
            {
                const float a = mono[(size_t) (start + i)], b = mono[(size_t) (start + i + lag)];
                num += a * b; d1 += a * a; d2 += b * b;
            }
            const float r = (float) (num / std::sqrt (d1 * d2 + 1e-12));
            if (r > best) { best = r; bestLag = lag; }
        }
        if (best > 0.8f && bestLag > 0)
        {
            s.rootHz = (float) (s.sr / bestLag);
            s.rootNote = 69.0f + 12.0f * std::log2 (s.rootHz / 440.0f);
        }
    }
}

void Mips::build (const juce::AudioBuffer<float>& b)
{
    // Half-band low-pass (Kaiser-windowed sinc, cutoff at half the band, 79 taps, every second
    // tap zero): flat to 0.43 of the band (-0.1 dB at 0.45), more than 90 dB down from 0.58. The
    // reader's own low-pass (0.9 / rate, rate under 2 on the chosen level) removes what lies between.
    static const std::vector<float> h = []
    {
        constexpr int half = 39;
        const double beta = 9.0, pi = 3.14159265358979323846;
        auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        std::vector<float> c ((size_t) half + 1);
        double sum = 0.0;
        for (int k = 0; k <= half; ++k)
        {
            const double x = 0.5 * k, sinc = k == 0 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double r = (double) k / (half + 1);
            c[(size_t) k] = (float) (0.5 * sinc * i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta));
            sum += k == 0 ? c[0] : 2.0 * c[(size_t) k];
        }
        for (auto& v : c) v = (float) (v / sum);
        return c;
    }();
    const int half = (int) h.size() - 1;
    lv.clear();
    lv.reserve ((size_t) kMax);
    for (int k = 0; k < kMax; ++k)
    {
        const auto& src = k == 0 ? b : lv.back();
        const int n = src.getNumSamples();
        if (n < 64) break;
        const int m = (n + 1) / 2;
        juce::AudioBuffer<float> d (src.getNumChannels(), m);
        for (int c = 0; c < src.getNumChannels(); ++c)
        {
            const float* x = src.getReadPointer (c);
            float* y = d.getWritePointer (c);
            for (int j = 0; j < m; ++j)
            {
                // Centred on source sample 2j (zero phase: position p at this level is 2p below).
                const int i = 2 * j;
                double acc = h[0] * x[i];
                for (int t = 1; t <= half; t += 2)
                {
                    const float a = i - t >= 0 ? x[i - t] : 0.0f, b2 = i + t < n ? x[i + t] : 0.0f;
                    acc += h[(size_t) t] * (a + b2);
                }
                y[j] = (float) acc;
            }
        }
        lv.push_back (std::move (d));
    }
}

// ---------------------------------------------------------------- VOICE's slices
namespace
{
bool wholeOf (const SampleData& s, const SampleRegion& reg) { return reg.start <= 0.0 && reg.end >= (double) s.length(); }
struct Onsets { int i0 = 0, i1 = 0; bool lead = true; };     // s.slices[i0..i1) lie in the region; lead: START starts slice 1
Onsets onsetsIn (const SampleData& s, const SampleRegion& reg)
{
    const auto b = s.slices.begin(), e = s.slices.end();
    auto below = [] (int a, double v) { return (double) a < v; };
    Onsets o;
    o.i0 = (int) (std::lower_bound (b, e, reg.start, below) - b);
    o.i1 = std::max (o.i0, (int) (std::lower_bound (b, e, reg.end, below) - b));
    o.lead = o.i1 == o.i0 || (double) s.slices[(size_t) o.i0] > reg.start + 0.04 * s.sr;
    return o;
}
} // namespace

int Slices::count (const SampleData& s, int mode, const SampleRegion& reg)
{
    if (mode == 0)
    {
        if (wholeOf (s, reg)) return std::max (1, (int) s.slices.size());
        const auto o = onsetsIn (s, reg);
        return o.i1 - o.i0 + (o.lead ? 1 : 0);
    }
    if (mode == 1) return 8;
    if (mode == 2) return 16;
    return 1;
}

double Slices::start (const SampleData& s, int mode, int k, const SampleRegion& reg)
{
    if (mode == 0)
    {
        if (wholeOf (s, reg)) return s.slices.empty() ? 0 : s.slices[(size_t) juce::jlimit (0, (int) s.slices.size() - 1, k)];
        const auto o = onsetsIn (s, reg);
        if (o.lead) { if (k <= 0) return reg.start; --k; }
        return o.i1 > o.i0 ? (double) s.slices[(size_t) (o.i0 + juce::jlimit (0, o.i1 - o.i0 - 1, k))] : reg.start;
    }
    if (mode == 1 || mode == 2)
    {
        if (wholeOf (s, reg)) return (int) ((long long) s.length() * k / (mode == 1 ? 8 : 16));
        return reg.start + reg.length() * k / (mode == 1 ? 8 : 16);
    }
    return reg.start;
}

SamplePtr makeSample (juce::AudioBuffer<float>&& audio, double fileSr, double targetSr, const juce::String& name, bool encode)
{
    auto s = std::make_shared<SampleData>();
    s->name = name;
    s->sr = targetSr;
    const int inCh = audio.getNumChannels();
    const int inN = audio.getNumSamples();
    if (inCh == 0 || inN == 0) return nullptr;

    if (encode)
    {
        // FLAC is integer: a floating-point file that peaks above full scale is stored scaled
        // down, with the gain to restore it, so it reopens exactly as loaded.
        float pk = 0.0f;
        for (int c = 0; c < inCh; ++c) pk = std::max (pk, audio.getMagnitude (c, 0, inN));
        juce::AudioBuffer<float> scaled;
        const juce::AudioBuffer<float>* src = &audio;
        if (pk > 0.999f)
        {
            s->flacGain = pk / 0.999f;
            scaled.makeCopyOf (audio);
            scaled.applyGain (1.0f / s->flacGain);
            src = &scaled;
        }
        juce::FlacAudioFormat flacFmt;
        auto* mos = new juce::MemoryOutputStream (s->flac, false);
        std::unique_ptr<juce::AudioFormatWriter> w (flacFmt.createWriterFor (mos, fileSr, (unsigned int) std::min (inCh, 2), 24, {}, 0));
        if (w != nullptr) w->writeFromAudioSampleBuffer (*src, 0, inN);
        else delete mos;
    }

    const double ratio = fileSr / targetSr;
    const int outN = (int) std::ceil (inN / ratio);
    s->buf.setSize (2, outN);
    for (int c = 0; c < 2; ++c)
    {
        const int src = std::min (c, inCh - 1);
        if (std::abs (ratio - 1.0) < 1e-9)
            s->buf.copyFrom (c, 0, audio, src, 0, inN);
        else if (c == 1 && inCh == 1)
            s->buf.copyFrom (1, 0, s->buf, 0, 0, outN);
        else
            SincResampler::process (audio.getReadPointer (src), inN, s->buf.getWritePointer (c), outN, fileSr, targetSr);
    }
    analyse (*s);
    s->mips.build (s->buf);
    // The displays' waveform: the largest |x| of either channel over each part, over the peak.
    s->overview.assign ((size_t) SampleData::kOverview, 0.0f);
    for (int k = 0, n = s->length(); k < SampleData::kOverview && n > 0; ++k)
    {
        const int a = (int) ((int64_t) n * k / SampleData::kOverview), b = std::max (a + 1, (int) ((int64_t) n * (k + 1) / SampleData::kOverview));
        float pk = 0.0f;
        for (int c = 0; c < 2; ++c) pk = std::max (pk, s->buf.getMagnitude (c, a, std::min (b, n) - a));
        s->overview[(size_t) k] = std::min (1.0f, pk / s->peak);
    }
    return s;
}

static SamplePtr readFrom (juce::AudioFormatReader* r, double targetSr, const juce::String& name, bool encode, double maxSeconds, float gain = 1.0f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (r);
    if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSeconds));
    juce::AudioBuffer<float> b ((int) std::max (1u, std::min (2u, reader->numChannels)), len);
    reader->read (&b, 0, len, 0, true, true);
    if (gain != 1.0f) b.applyGain (gain);
    return makeSample (std::move (b), reader->sampleRate, targetSr, name, encode);
}

SamplePtr loadSampleFile (const juce::File& f, double targetSr, double maxSeconds)
{
    return readFrom (formats().createReaderFor (f), targetSr, f.getFileNameWithoutExtension(), true, maxSeconds);
}

SamplePtr loadSampleFlac (const juce::MemoryBlock& mb, const juce::String& name, double targetSr, float gain)
{
    juce::FlacAudioFormat flacFmt;
    auto* r = flacFmt.createReaderFor (new juce::MemoryInputStream (mb, true), true);
    if (r == nullptr) return nullptr;
    auto s = readFrom (r, targetSr, name, false, 60.0 * 20.0, gain);
    if (s != nullptr) { const_cast<SampleData&> (*s).flac = mb; const_cast<SampleData&> (*s).flacGain = gain; }
    return s;
}

void SampleSlot::set (SamplePtr p)
{
    const std::lock_guard<std::mutex> g (lock);
    const auto now = juce::Time::getMillisecondCounter();
    if (owned != nullptr) retired.push_back ({ owned, now });
    owned = std::move (p);
    live.store (owned.get(), std::memory_order_release);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Retired& r) { return now - r.at > 2000; }), retired.end());
}

void SampleSlot::save (juce::ValueTree& t, const juce::Identifier& key) const
{
    if (auto s = get())
        if (s->flac.getSize() > 0)
        {
            juce::ValueTree c (key);
            c.setProperty ("name", s->name, nullptr);
            c.setProperty ("flac", s->flac, nullptr);
            if (s->flacGain != 1.0f) c.setProperty ("gain", s->flacGain, nullptr);
            t.appendChild (c, nullptr);
        }
}

void SampleSlot::load (const juce::ValueTree& t, const juce::Identifier& key, double sr)
{
    auto c = t.getChildWithName (key);
    if (! c.isValid()) { set (nullptr); return; }
    if (auto* mb = c.getProperty ("flac").getBinaryData())
        set (loadSampleFlac (*mb, c.getProperty ("name").toString(), sr, (float) (double) c.getProperty ("gain", 1.0)));
}
} // namespace sf
