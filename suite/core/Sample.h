#pragma once
// Sample loading, analysis and embedding (FLAC inside the Live set), shared by every sampler.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include "BandLimited.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace sf
{
// Octave-down copies of a stereo buffer (each a half-band low-pass and every second sample), built
// off the audio thread. A read far above the original pitch uses the copy where the rate is
// between 1 and 2, so the band-limited read costs at most about 36 taps however high a sample is
// played (reading the original at rate r costs about 18 r).
struct Mips
{
    static constexpr int kMax = 12;                 // rates up to 8192
    std::vector<juce::AudioBuffer<float>> lv;       // lv[k]: decimated by 2^(k+1)
    void build (const juce::AudioBuffer<float>& b);
};

// Band-limited read of channel ch (clamped to what the buffer has) at a fractional position,
// for any playback rate (input samples per output sample). Audio thread; allocation-free.
inline float readAt (const juce::AudioBuffer<float>& b, const Mips& m, int ch, double pos, double rate)
{
    const juce::AudioBuffer<float>* src = &b;
    for (size_t k = 0; rate > 2.0 && k < m.lv.size(); ++k) { src = &m.lv[k]; rate *= 0.5; pos *= 0.5; }
    return sampleAt (src->getReadPointer (std::min (ch, src->getNumChannels() - 1)), src->getNumSamples(), pos, rate);
}

struct SampleData
{
    static constexpr int kOverview = 1024;
    juce::AudioBuffer<float> buf;   // stereo, at the module's sample rate
    double sr = 48000.0;
    juce::String name;
    float rootHz = 0.0f;            // 0 = no clear pitch
    float rootNote = 60.0f;         // MIDI note (from rootHz), 60 if unpitched
    float decaySec = 0.5f;          // time to fall 40 dB from the peak
    std::vector<int> slices;        // onset positions (samples), first = first onset
    juce::MemoryBlock flac;         // embedded copy for the Live set
    float flacGain = 1.0f;          // the copy is scaled to fit FLAC's range: multiply back by this
    float peak = 1.0f;
    Mips mips;                      // octave-down copies of buf (readAt)
    std::vector<float> overview;    // kOverview parts: the largest |x| of either channel in each, over the peak (displays)
    int length() const { return buf.getNumSamples(); }
    float read (int ch, double pos, double rate) const { return readAt (buf, mips, ch, pos, rate); }
};
using SamplePtr = std::shared_ptr<const SampleData>;

// The part of a sample that plays: START to END (fractions of it), FADE IN after START and FADE OUT
// before END (raised cosine). The fades are in the sample's own time (ms of it as recorded): played
// higher, they pass faster, as the sample does. A point inside the sample always fades for 1 ms at
// least, so a cut into a waveform never clicks; at the sample's own ends a fade of 0 is none. At
// the defaults (0, 100 %, 0, 0) it is the whole sample, no fades: as before these existed, to the bit.
// (Knob values within a hair of a default count as it: smoothing settles a step short of a target.)
struct SampleRegion
{
    double start = 0.0, end = 0.0, fadeIn = 0.0, fadeOut = 0.0;    // in the sample's samples; end: one past
    static SampleRegion of (const SampleData& s, float startFrac, float endFrac, float fadeInMs, float fadeOutMs)
    {
        SampleRegion r;
        const double n = (double) s.length(), one = std::max (2.0, 0.001 * s.sr);
        if (n <= 0.0) return r;
        const bool cutStart = startFrac > 1.0e-4f, cutEnd = endFrac < 0.9999f;
        r.start = cutStart ? std::min ((double) juce::jlimit (0.0f, 1.0f, startFrac) * n, std::max (0.0, n - one)) : 0.0;
        r.end = cutEnd ? std::min (n, std::max ((double) juce::jlimit (0.0f, 1.0f, endFrac) * n, r.start + one)) : n;
        r.fadeIn = fadeInMs > 0.01f ? (double) fadeInMs * 0.001 * s.sr : 0.0;
        r.fadeOut = fadeOutMs > 0.01f ? (double) fadeOutMs * 0.001 * s.sr : 0.0;
        if (cutStart) r.fadeIn = std::max (r.fadeIn, 0.001 * s.sr);
        if (cutEnd) r.fadeOut = std::max (r.fadeOut, 0.001 * s.sr);
        if (const double len = r.end - r.start, both = r.fadeIn + r.fadeOut; both > len && both > 0.0) { r.fadeIn *= len / both; r.fadeOut *= len / both; }
        return r;
    }
    static SampleRegion whole (const SampleData& s) { SampleRegion r; r.end = (double) s.length(); return r; }
    double length() const { return end - start; }
    int last() const { return (int) end; }      // a read stops 2 samples before it, as at the sample's end
    // The fades' gain at a position (1 outside them).
    float gain (double pos) const
    {
        float g = 1.0f;
        if (fadeIn > 0.0 && pos < start + fadeIn) g = rc ((pos - start) / fadeIn);
        if (fadeOut > 0.0 && pos > end - fadeOut) g *= rc ((end - pos) / fadeOut);
        return g;
    }
    static float rc (double x) { return x <= 0.0 ? 0.0f : x >= 1.0 ? 1.0f : (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x)); }
};

// VOICE's slices of a phrase, in the part of it that plays (reg); allocation-free (audio thread).
// mode: 0 AUTO, 1 EQUAL 8, 2 EQUAL 16, 3 WHOLE. AUTO: the onsets from START to END, START itself the
// first slice's start unless an onset follows it within 40 ms (as the analysis does at the phrase's
// own start). EQUAL 8 / 16: START to END in equal parts. WHOLE: one, from START. The whole phrase
// (START 0, END 100 %): the slices as they were before regions, to the sample.
struct Slices
{
    static int count (const SampleData& s, int mode, const SampleRegion& reg);
    static double start (const SampleData& s, int mode, int k, const SampleRegion& reg);    // in the phrase's samples
};

// Decode, resample to `targetSr` (windowed sinc), analyse. Returns null on failure.
// Message/background thread. Files longer than maxSeconds are cut there.
SamplePtr loadSampleFile (const juce::File&, double targetSr, double maxSeconds = 60.0);
SamplePtr loadSampleFlac (const juce::MemoryBlock&, const juce::String& name, double targetSr, float gain = 1.0f);
SamplePtr makeSample (juce::AudioBuffer<float>&& audio, double fileSr, double targetSr, const juce::String& name, bool encode);

// Sample holder shared by the message thread (set, save, paint) and the audio thread (audio()).
// The audio thread only ever loads a raw pointer (lock-free, wait-free); a replaced sample is
// kept alive on the message thread for 2 s, long after any audio callback that saw it ended.
class SampleSlot
{
public:
    const SampleData* audio() const noexcept { return live.load (std::memory_order_acquire); }   // audio thread
    // Everything below may be called from the message thread, the host's prepare / state threads
    // or a loader: guarded by a mutex the audio thread never touches.
    SamplePtr get() const { const std::lock_guard<std::mutex> g (lock); return owned; }
    void set (SamplePtr p);
    void save (juce::ValueTree& t, const juce::Identifier& key) const;
    void load (const juce::ValueTree& t, const juce::Identifier& key, double sr);
private:
    std::atomic<const SampleData*> live { nullptr };
    mutable std::mutex lock;
    SamplePtr owned;
    struct Retired { SamplePtr p; juce::uint32 at; };
    std::vector<Retired> retired;
};

// Anything built from a sample for the audio thread to read (WAVETABLE's frames): swapped in whole
// as SampleSlot swaps samples, the old one kept 2 s for a block that may still be reading it.
template <typename T>
class SharedSlot
{
public:
    const T* audio() const noexcept { return live.load (std::memory_order_acquire); }
    void set (std::shared_ptr<const T> p)
    {
        const std::lock_guard<std::mutex> g (lock);
        const auto now = juce::Time::getMillisecondCounter();
        if (owned != nullptr) retired.push_back ({ owned, now });
        owned = std::move (p);
        live.store (owned.get(), std::memory_order_release);
        retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Retired& r) { return now - r.at > 2000; }), retired.end());
    }
private:
    std::atomic<const T*> live { nullptr };
    std::mutex lock;
    std::shared_ptr<const T> owned;
    struct Retired { std::shared_ptr<const T> p; juce::uint32 at; };
    std::vector<Retired> retired;
};

// Audio file extensions every sampler accepts.
inline bool isAudioFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".aif") || path.endsWithIgnoreCase (".aiff")
        || path.endsWithIgnoreCase (".flac") || path.endsWithIgnoreCase (".mp3") || path.endsWithIgnoreCase (".ogg")
        || path.endsWithIgnoreCase (".m4a");
}
} // namespace sf
