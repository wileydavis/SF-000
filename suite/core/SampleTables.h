#pragma once
// (SF-000) What a wavetable can be made of a recording, for any engine that reads tables (WAVETABLE on the synth lanes,
// SYNTH on every lane): sixty-four frames along a recording (fine), and the file read as a wavetable file (file). Moved here
// from modules/Synth.cpp in the hundred and second build, unchanged, so that SYNTH can build them on every lane.
#include "Wavetable.h"
#include "Sample.h"
#include <thread>
#include <atomic>
#include <mutex>

namespace sf
{
namespace tables
{
// WAVETABLE's TABLE SAMPLE [Nov 10]: eight frames from the loaded sample, at the voices' rate,
// from evenly across it, start to end. Each frame is four periods of the sound where it is cut:
// the period measured there (normalised autocorrelation, 25 Hz-2 kHz, the shortest lag within
// 10 % of the best so an octave below isn't taken, refined between lags); where nothing is
// periodic enough, the sample's analysed root or 1024 samples at 48 kHz. Hann-windowed, read as
// harmonics (Mipmap::harmonicsOfCycles: a cycle that doesn't join up with itself doesn't buzz),
// turned so every frame's fundamental starts at the same phase (frames blend without
// cancelling), brought to one loudness; a frame 40 dB under the loudest (silence at an end)
// takes its neighbour's.
inline double periodAt (const std::vector<float>& x, int t0, int w, double sr)
{
    int n = 1; while (n < 2 * w) n <<= 1;
    std::vector<std::complex<double>> X ((size_t) n);
    for (int i = 0; i < w; ++i) X[(size_t) i] = x[(size_t) (t0 + i)];
    Mipmap::transform (X, false);
    for (auto& v : X) v = std::norm (v);
    Mipmap::transform (X, true);
    std::vector<double> c ((size_t) w + 1, 0.0);
    for (int i = 0; i < w; ++i) c[(size_t) i + 1] = c[(size_t) i] + (double) x[(size_t) (t0 + i)] * x[(size_t) (t0 + i)];
    const int lo = std::max (2, (int) (sr / 2000.0)), hi = std::min (w / 2, (int) (sr / 25.0));
    if (hi <= lo + 2 || c[(size_t) w] < 1.0e-9) return 0.0;
    std::vector<double> r ((size_t) hi + 2, 0.0);
    double best = 0.0;
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        const double e = std::sqrt ((c[(size_t) (w - lag)] - c[0]) * (c[(size_t) w] - c[(size_t) lag])) + 1.0e-20;
        r[(size_t) lag] = X[(size_t) lag].real() / (double) n / e;
        if (lag >= lo && lag <= hi) best = std::max (best, r[(size_t) lag]);
    }
    if (best < 0.6) return 0.0;
    for (int lag = lo; lag <= hi; ++lag)
        if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
        {
            const double a = r[(size_t) lag - 1], b = r[(size_t) lag], d = r[(size_t) lag + 1], den = a - 2.0 * b + d;
            return lag + (std::abs (den) > 1.0e-12 ? 0.5 * (a - d) / den : 0.0);
        }
    return 0.0;
}
// (SF-000) Sixty-four frames from a recording (TABLE SAMPLE had eight until the eighty-seventh build): each four periods
// of the sound where it is cut, windowed, read as harmonics, every frame's fundamental at one phase, each brought to
// one loudness; at sixty-four places evenly along it, and how loud the recording was at each kept beside them (LEVELS).
// (the hundred and second build: frames, how many; SYNTH's SAMPLE and RECORDING tables have one each hundredth of a second
// of the recording, scanFrames. WAVETABLE's stay sixty-four.)
inline std::shared_ptr<const Wavetable> fineFrom (const SampleData& s, double voiceRate, int frames = 64)
{
    const int len = s.length(), chans = std::max (1, s.buf.getNumChannels());
    const int kF = std::max (2, frames);
    constexpr int kN = 1024, kCycles = 4, kH = Wavetable::kMaxHarm;
    const double fallback = s.rootHz > 20.0f ? s.sr / (double) s.rootHz : 1024.0 * s.sr / 48000.0;
    const int win = (int) (0.085 * s.sr);
    if (len < (int) (fallback * kCycles) + win + 8) return nullptr;
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) { float v = 0.0f; for (int ch = 0; ch < chans; ++ch) v += s.buf.getSample (ch, i); mono[(size_t) i] = v / (float) chans; }
    auto at = [&mono, len] (double t)
    {
        const int i = juce::jlimit (1, len - 3, (int) t);
        const float fr = (float) (t - i), xm1 = mono[(size_t) i - 1], x0 = mono[(size_t) i], x1 = mono[(size_t) i + 1], x2 = mono[(size_t) i + 2];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return (double) (((c3 * fr + c2) * fr + c1) * fr + x0);
    };
    std::vector<std::vector<float>> amps ((size_t) kF), phs ((size_t) kF);
    std::vector<double> rms ((size_t) kF, 0.0), x ((size_t) (kN * kCycles));
    const double usable = (double) len - 8.0 - win - fallback * kCycles;
    // (the period is measured at every fourth frame and at the last, and taken from the two nearest between them: a
    // recording's pitch does not move far in a sixty-fourth of its length, and the measuring is most of the work)
    std::vector<double> period ((size_t) kF, -1.0);
    for (int f = 0; f < kF; f = f < kF - 1 ? std::min (kF - 1, f + 4) : kF) period[(size_t) f] = periodAt (mono, (int) (2.0 + std::max (0.0, usable) * f / (kF - 1)), win, s.sr);
    for (int f = 0; f < kF; ++f)
    {
        if (period[(size_t) f] >= 0.0) continue;
        int a = f, b = f;
        while (period[(size_t) a] < 0.0) --a;
        while (period[(size_t) b] < 0.0) ++b;
        const double pa = period[(size_t) a], pb = period[(size_t) b];
        period[(size_t) f] = pa > 0.0 && pb > 0.0 ? pa + (pb - pa) * (double) (f - a) / (double) (b - a) : pa > 0.0 ? pa : pb > 0.0 ? pb : -2.0;
    }
    for (int f = 0; f < kF; ++f)
    {
        const double t0 = 2.0 + std::max (0.0, usable) * f / (kF - 1);
        double per = period[(size_t) f];
        if (per <= 0.0) per = fallback;
        per = std::min (per, ((double) len - 8.0 - t0) / kCycles);
        for (size_t i = 0; i < x.size(); ++i) x[i] = at (t0 + ((double) i + 0.5) * per / kN);
        auto& amp = amps[(size_t) f]; auto& ph = phs[(size_t) f];
        Mipmap::harmonicsOfCycles (x, kCycles, kH, amp, ph);
        rms[(size_t) f] = Wavetable::rmsOf (amp);
        const float p1 = ph[0];
        for (size_t h = 0; h < ph.size(); ++h) ph[h] -= (float) (h + 1) * p1;
    }
    const double loud = *std::max_element (rms.begin(), rms.end());
    if (loud < 1.0e-6) return nullptr;
    std::vector<float> lev ((size_t) kF);
    for (int f = 0; f < kF; ++f) lev[(size_t) f] = (float) (rms[(size_t) f] / loud);
    // (a silent frame takes its nearest loud neighbour's wave, as the eight do; its own level is kept beside it)
    for (int f = 0; f < kF; ++f)
        if (rms[(size_t) f] < 0.01 * loud)
            for (int d = 1; d < kF; ++d)
            {
                const int k = f - d >= 0 && rms[(size_t) (f - d)] >= 0.01 * loud ? f - d : f + d < kF && rms[(size_t) (f + d)] >= 0.01 * loud ? f + d : -1;
                if (k >= 0) { amps[(size_t) f] = amps[(size_t) k]; phs[(size_t) f] = phs[(size_t) k]; break; }
            }
    for (auto& a : amps) Wavetable::toRms (a);
    auto out = std::make_shared<Wavetable>();
    out->build (amps, phs, voiceRate);
    out->level = lev;
    out->name = s.name;
    return out;
}
// (SF-000) TABLE FILE: the loaded file is a wavetable, as Serum, Vital and Phase Plant write them: 2048 samples a
// frame, up to 256 frames. It is read from the file's own samples (the copy kept with the sample), not from the
// sample as it was brought to the rig's rate. A file of 4096 samples or fewer that is not a whole number of frames is
// one cycle; a file whose length is a multiple of 1024 and not of 2048 has 1024 a frame. The frames keep their levels
// against each other. What it made of the file, or why it could not, is `says`.
inline std::shared_ptr<const Wavetable> fileFrom (const SampleData& s, double voiceRate, juce::String& says, bool& whole)
{
    whole = false;
    juce::AudioBuffer<float> raw;
    if (s.flac.getSize() > 0)
    {
        juce::FlacAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (new juce::MemoryInputStream (s.flac, false), true));
        if (r != nullptr && r->lengthInSamples > 0)
        {
            if (r->lengthInSamples > 256 * 2048) { says = "NOT A WAVETABLE FILE (LONGER THAN 256 FRAMES OF 2048): TABLE SAMPLE READS A RECORDING"; return nullptr; }
            raw.setSize ((int) std::max (1u, std::min (2u, r->numChannels)), (int) r->lengthInSamples);
            r->read (&raw, 0, (int) r->lengthInSamples, 0, true, true);
        }
    }
    if (raw.getNumSamples() == 0)
    {
        if (s.length() > 256 * 2048) { says = "NOT A WAVETABLE FILE (LONGER THAN 256 FRAMES OF 2048): TABLE SAMPLE READS A RECORDING"; return nullptr; }
        raw.makeCopyOf (s.buf);
    }
    const int n = raw.getNumSamples(), chans = raw.getNumChannels();
    if (n < 16) { says = "NOT A WAVETABLE FILE (TOO SHORT)"; return nullptr; }
    std::vector<double> mono ((size_t) n);
    for (int i = 0; i < n; ++i) { double v = 0.0; for (int ch = 0; ch < chans; ++ch) v += raw.getSample (ch, i); mono[(size_t) i] = v / chans; }
    int size = 2048, count = n / 2048;
    bool one = false;
    if (n % 2048 != 0)
    {
        if (n <= 4096) { one = true; count = 1; }
        else if (n % 1024 == 0) { size = 1024; count = n / 1024; }
    }
    if (count < 1) { one = true; count = 1; }
    whole = one ? n <= 4096 : n % size == 0;        // (a whole number of frames, or one short cycle: a file made as a wavetable)
    const int H = std::min ((int) Wavetable::kMaxHarm, size / 2 - 1);
    std::vector<std::vector<float>> amps ((size_t) count), phs ((size_t) count);
    std::vector<double> cyc ((size_t) size);
    double loud = 0.0;
    for (int f = 0; f < count; ++f)
    {
        if (one)
            // (one cycle of any length: read at 2048 even steps round it)
            for (int i = 0; i < size; ++i)
            {
                const double t = ((double) i + 0.5) * n / size - 0.5;
                const int i0 = (int) std::floor (t); const double fr = t - i0;
                auto v = [&] (int k) { return mono[(size_t) (((k % n) + n) % n)]; };
                const double xm1 = v (i0 - 1), x0 = v (i0), x1 = v (i0 + 1), x2 = v (i0 + 2);
                cyc[(size_t) i] = ((0.5 * (x2 - xm1) + 1.5 * (x0 - x1)) * fr + (xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2)) * fr * fr + 0.5 * (x1 - xm1) * fr + x0;
            }
        else
            for (int i = 0; i < size; ++i) cyc[(size_t) i] = mono[(size_t) (f * size + i)];
        Mipmap::harmonicsOf (cyc, H, amps[(size_t) f], phs[(size_t) f]);
        loud = std::max (loud, Wavetable::rmsOf (amps[(size_t) f]));
    }
    if (loud < 1.0e-6) { says = "NOT A WAVETABLE FILE (IT IS SILENT)"; return nullptr; }
    for (auto& a : amps) for (auto& v : a) v = (float) (v * 0.5 / loud);
    auto out = std::make_shared<Wavetable>();
    out->build (amps, phs, voiceRate);
    out->name = s.name;
    says = one ? juce::String ("ONE CYCLE OF ") + juce::String (n) + " SAMPLES"
               : juce::String (count) + (count == 1 ? " FRAME OF " : " FRAMES OF ") + juce::String (size) + (n % size != 0 ? " (" + juce::String (n % size) + " SAMPLES OVER, LEFT OUT)" : juce::String());
    return out;
}
// (SF-000, the hundred and nineteenth build) CHOP: frames as Ableton's Wavetable makes them of a sample, its "Raw" off (its
// own words, of what it does: "Any leading or trailing silence is ignored", "The edges of each wavetable are faded to zero to
// avoid clicks", "The phase differences between neighboring wavetables are minimized", "The wavetables are normalized"):
// slices of 1024 of the recording's samples one after another from where its sound begins, up to 256 (Live's own limits),
// a silent slice passed over. No pitch is measured: a slice is rarely a whole number of the note's cycles, so each frame
// has some grit of its own, and moving along them moves with the recording's rhythm, slice by slice. Each is faded at its
// edges (64 samples), read as harmonics, turned round as a whole to lie closest to the one before (its phases, not its
// sound: a cycle's start is anywhere), and brought to one loudness, its level as it was kept beside it (RECORDING).
// seconds: how much of the recording the frames are; start: where in it they begin (SF-000, the display's strip).
inline std::shared_ptr<const Wavetable> chopFrom (const SampleData& s, double voiceRate, double& seconds, double& start)
{
    constexpr int kSlice = 1024, kMost = 256, kFade = 64, kH = kSlice / 2 - 1;
    const int len = s.length(), chans = std::max (1, s.buf.getNumChannels());
    seconds = 1.0; start = 0.0;
    if (len < 2 * kSlice) return nullptr;
    std::vector<double> mono ((size_t) len);
    double peak = 0.0;
    for (int i = 0; i < len; ++i) { double v = 0.0; for (int ch = 0; ch < chans; ++ch) v += s.buf.getSample (ch, i); mono[(size_t) i] = v / chans; peak = std::max (peak, std::abs (mono[(size_t) i])); }
    if (peak < 1.0e-6) return nullptr;
    // where its sound begins and ends (-50 dB of its peak)
    int from = 0, to = len;
    while (from < len && std::abs (mono[(size_t) from]) < peak * 0.00316) ++from;
    while (to > from && std::abs (mono[(size_t) to - 1]) < peak * 0.00316) --to;
    const int count = std::min (kMost, (to - from) / kSlice);
    if (count < 2) return nullptr;
    start = (double) from / s.sr;
    std::vector<std::vector<float>> amps, phs;
    std::vector<double> rms;
    std::vector<double> cyc ((size_t) kSlice);
    double loud = 0.0;
    std::vector<std::vector<double>> slices;
    for (int f = 0; f < count; ++f)
    {
        double e = 0.0;
        for (int i = 0; i < kSlice; ++i)
        {
            const double w = i < kFade ? 0.5 - 0.5 * std::cos (3.14159265358979323846 * (i + 0.5) / kFade)
                           : i >= kSlice - kFade ? 0.5 - 0.5 * std::cos (3.14159265358979323846 * (kSlice - i - 0.5) / kFade) : 1.0;
            cyc[(size_t) i] = mono[(size_t) (from + f * kSlice + i)] * w;
            e += cyc[(size_t) i] * cyc[(size_t) i];
        }
        slices.push_back (cyc);
        rms.push_back (std::sqrt (e / kSlice));
        loud = std::max (loud, rms.back());
    }
    std::vector<float> lev;
    for (size_t f = 0; f < slices.size(); ++f)
    {
        if (rms[f] < 0.01 * loud) continue;                // (silence passed over)
        std::vector<float> a, ph;
        Mipmap::harmonicsOf (slices[f], kH, a, ph);
        // turned round to lie closest to the frame before: the shift whose harmonics agree with its the most
        if (! amps.empty())
        {
            const auto& pa = amps.back(); const auto& pp = phs.back();
            const int H = std::min (64, kH);
            double best = -1.0e30, at = 0.0;
            for (int k = 0; k < 256; ++k)
            {
                const double th = 2.0 * 3.14159265358979323846 * k / 256.0;
                double c = 0.0;
                for (int h = 1; h <= H; ++h) c += (double) a[(size_t) h - 1] * pa[(size_t) h - 1] * std::cos ((double) ph[(size_t) h - 1] + h * th - (double) pp[(size_t) h - 1]);
                if (c > best) { best = c; at = th; }
            }
            for (int h = 1; h <= kH; ++h) ph[(size_t) h - 1] += (float) (h * at);
        }
        lev.push_back ((float) (rms[f] / loud));
        Wavetable::toRms (a);
        amps.push_back (std::move (a)); phs.push_back (std::move (ph));
    }
    if (amps.size() < 2) return nullptr;
    auto out = std::make_shared<Wavetable>();
    out->build (amps, phs, voiceRate);
    out->level = lev;
    out->name = s.name;
    seconds = std::max (0.01, (double) (count * kSlice) / s.sr);
    return out;
}
// (SF-000, the hundred and second build) SYNTH's frames along a recording: one each hundredth of a second, so that a note
// moving slowly through it, or held at one place, has the sound that was there (a syllable, a breath); sixty-four at
// least, 1024 at most (a recording over ten seconds has them further apart: about 30 ms in thirty seconds).
inline int scanFrames (const SampleData& s) { return juce::jlimit (64, 1024, (int) std::ceil ((double) s.length() / s.sr / 0.01)); }
// What WAVETABLE can make of the loaded sample: its sixty-four frames, and the file read as a wavetable; and SYNTH's
// frames along it (scan) and how long the recording is.
// Made once for a sample and a rate, and shared by every lane and voice that holds that sample.
struct SampleTables { std::shared_ptr<const Wavetable> fine, file; juce::String fileSays; bool fileWhole = false; };
inline std::shared_ptr<const SampleTables> tablesFrom (const SamplePtr& s, double voiceRate)
{
    struct Kept { std::weak_ptr<const SampleData> of; double rate = 0.0; std::weak_ptr<const SampleTables> made; };
    static std::mutex lock;
    static std::vector<Kept> kept;
    {
        const std::lock_guard<std::mutex> g (lock);
        kept.erase (std::remove_if (kept.begin(), kept.end(), [] (const Kept& k) { return k.of.expired() || k.made.expired(); }), kept.end());
        for (auto& k : kept) if (k.rate == voiceRate && k.of.lock() == s) if (auto m = k.made.lock()) return m;
    }
    auto t = std::make_shared<SampleTables>();
    t->fine = fineFrom (*s, voiceRate);
    t->file = fileFrom (*s, voiceRate, t->fileSays, t->fileWhole);
    const std::lock_guard<std::mutex> g (lock);
    kept.push_back ({ s, voiceRate, t });
    return t;
}
// (SF-000, the hundred and second build) SYNTH's frames along a recording, made once for a sample and a rate and shared.
// A long recording's take a good part of a second (1024 frames: 0.6 s on the build machine), so SYNTH has them made on a
// thread of their own (SynthEngine::fileChanged) unless asyncScans is off, as the checks and the command line have it: a
// note played before they are ready plays BASIC.
struct Scan { std::shared_ptr<const Wavetable> table, chop; double seconds = 1.0, chopSeconds = 1.0, chopStart = 0.0; std::vector<float> overview; };      // (chop: CHOP's frames; overview: the recording's peak, 512 columns, for the display)
inline std::atomic<bool> asyncScans { true };
inline std::atomic<int> scansBuilding { 0 };
inline std::shared_ptr<const Scan> scanFrom (const SamplePtr& s, double voiceRate)
{
    struct Kept { std::weak_ptr<const SampleData> of; double rate = 0.0; std::weak_ptr<const Scan> made; };
    static std::mutex lock;
    static std::vector<Kept> kept;
    {
        const std::lock_guard<std::mutex> g (lock);
        kept.erase (std::remove_if (kept.begin(), kept.end(), [] (const Kept& k) { return k.of.expired() || k.made.expired(); }), kept.end());
        for (auto& k : kept) if (k.rate == voiceRate && k.of.lock() == s) if (auto m = k.made.lock()) return m;
    }
    auto t = std::make_shared<Scan>();
    t->table = fineFrom (*s, voiceRate, scanFrames (*s));
    t->seconds = std::max (0.01, (double) s->length() / s->sr);
    t->chop = chopFrom (*s, voiceRate, t->chopSeconds, t->chopStart);
    {
        // the recording's peak, 512 columns of it (for SYNTH's display: where along it the frames are)
        const int len = s->length(), chans = std::max (1, s->buf.getNumChannels());
        t->overview.assign (512, 0.0f);
        for (int i = 0; i < len; ++i)
        {
            float v = 0.0f; for (int ch = 0; ch < chans; ++ch) v = std::max (v, std::abs (s->buf.getSample (ch, i)));
            auto& o = t->overview[(size_t) std::min (511, (int) ((long long) i * 512 / std::max (1, len)))];
            o = std::max (o, v);
        }
    }
    const std::lock_guard<std::mutex> g (lock);
    kept.push_back ({ s, voiceRate, t });
    return t;
}
// (the checks: wait until every scan being made is in)
inline void waitForScans() { while (scansBuilding.load() > 0) std::this_thread::sleep_for (std::chrono::milliseconds (5)); }
} // namespace tables
} // namespace sf
