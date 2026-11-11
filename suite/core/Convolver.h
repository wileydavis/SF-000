#pragma once
// Partitioned convolution for SF TERROIR: uniformly partitioned overlap-save in two levels.
//   Head: taps 0-4095 in 512-sample blocks (8 partitions). Its latency, 512 samples, is
//         Pocket::kLatency, the latency the module reports to Live.
//   Tail: taps 4096 on, in 4096-sample blocks, fed the input 512 samples late, so its first tap
//         lands exactly where the head's last one stops. The products for the next tail block
//         are spread over the current one (only partition 0 waits for the block line), so no
//         chunk carries a whole block's work.
// Mono in (the module sends the sum), stereo out (one IR per output channel). The input spectra
// (the frequency-domain delay lines) do not depend on the IR, so a new IR takes over with the
// whole input history: both IRs are computed for one tail block and crossfaded (raised cosine,
// 4096 samples), and the old IR's tail is never cut off. Every sum runs in the same order however
// the chunks fall, so the output is bit-identical at any block size.
// Audio thread: no allocation, no locks. IRs are built elsewhere (makeIrFilter) and handed over.
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sf
{
// One impulse response, cut into the engine's partitions and transformed. Built off the audio thread.
struct IrFilter
{
    static constexpr int kB1 = 512, kP1 = 8, kB2 = 4096;
    static constexpr int kN1 = 2 * kB1, kN2 = 2 * kB2;          // FFT sizes
    static constexpr int kBins1 = kB1 + 1, kBins2 = kB2 + 1;
    static constexpr int kT1 = kB1 * kP1;                        // the first tail tap (4096)
    int taps = 0, p1 = 0, p2 = 0;                               // length; head and tail partitions in use
    int cap2 = 0;                                                // tail partitions it has room for (p2 at most)
    std::vector<float> h1re, h1im;                               // [ch][p][bin], kP1 partitions
    std::vector<float> h2re, h2im;                               // [ch][p][bin], cap2 partitions
    uint64_t tag = 0;                                            // the module's order of its IRs (TERROIR: epoch, request)
    std::array<int64_t, 10> from {};                             // what the module built it from (TERROIR: its key)
    // The module's own, for cutting this IR again on the audio thread (Convolver::transform): what
    // it was cut from (TERROIR: its first IR, before LENGTH's cut), as much of it as the engine
    // takes; its whole length, and its gain. Empty unless the module kept it.
    std::vector<float> uncut[2];
    int uncutLen = 0;
    float uncutGain = 1.0f;
    const float* head (int ch, int p, bool im) const { return (im ? h1im : h1re).data() + ((size_t) ch * kP1 + (size_t) p) * kBins1; }
    const float* tail (int ch, int p, bool im) const { return (im ? h2im : h2re).data() + ((size_t) ch * (size_t) cap2 + (size_t) p) * kBins2; }
    static int headParts (int taps) { return std::min (kP1, (std::max (0, taps) + kB1 - 1) / kB1); }
    static int tailParts (int taps) { return taps > kT1 ? (taps - kT1 + kB2 - 1) / kB2 : 0; }
};

// Two channels of `taps` samples, with room for capacity taps (a later cut, longer: Convolver::
// transform). Message / background thread.
std::unique_ptr<IrFilter> makeIrFilter (const float* irL, const float* irR, int taps, int capacity = 0);

class Convolver
{
public:
    static constexpr int kLatency = IrFilter::kB1;

    // maxTaps: the longest IR this engine will be handed (sets the tail's history).
    void prepare (int maxTaps);
    // Silence: history and outputs to zero. A running swap completes at once.
    void reset();
    // Audio thread. Takes f when free (no swap running or waiting, the last old IR collected);
    // returns false otherwise (offer it again later). The first IR is used at once.
    bool offer (IrFilter* f);
    // Audio thread: the IR a finished swap replaced (free it elsewhere), or null.
    IrFilter* takeRetired() { auto* r = retired; retired = nullptr; return r; }
    // Every IR it holds (in use, coming in, waiting, retired), forgotten. Not the audio thread.
    std::vector<IrFilter*> releaseAll();
    // Audio thread: f in use at once in place of everything it holds (no crossfade: for when nothing
    // has gone in since reset). What it held goes to out (at most 4; free them elsewhere); returns how many.
    int replaceNow (IrFilter* f, IrFilter** out);
    bool busy() const { return state != Idle || waiting != nullptr || retired != nullptr; }
    const IrFilter* current() const { return set[s0]; }
    IrFilter* currentForCut() { return set[s0]; }
    // Audio thread, between process() calls: partition p (head: kB1 samples of x; tail: kB2) of
    // channel ch of f, from the time domain, as makeIrFilter makes it. For cutting an IR again
    // where its new partitions are used only after the cut (no allocation: the engine's own FFTs).
    void transform (IrFilter& f, int ch, bool tail, int p, const float* x);
    int maxTaps() const { return maxP2 * IrFilter::kB2 + IrFilter::kT1; }
    // in: mono; outL / outR: the wet signal, kLatency samples late.
    void process (const float* in, float* outL, float* outR, int n);

private:
    enum State { Idle, Prep, Xfade };
    int release (IrFilter** out);                // every IR it holds, forgotten (at most 4)
    void headBoundary();
    void tailBoundary();
    void accumulate (int s, int upTo);           // tail partitions 1..upTo-1 of set slot s, in order
    void spread();                               // the share of the tail work due by now
    float* acc (int s, int ch, bool im) { return (im ? accIm : accRe).data() + ((size_t) s * 2 + (size_t) ch) * IrFilter::kBins2; }
    float* fdl2 (int k, bool im) { return (im ? fdl2Im : fdl2Re).data() + (size_t) k * IrFilter::kBins2; }
    float* fdl1 (int k, bool im) { return (im ? fdl1Im : fdl1Re).data() + (size_t) k * IrFilter::kBins1; }

    std::unique_ptr<juce::dsp::FFT> fft1, fft2;
    std::vector<float> work;                     // 2 * kN2 (JUCE's in-place real transforms)
    std::vector<float> hIn, tIn, tDelay;         // head frame (2 B1), tail frame (2 B2), tail input delay
    std::vector<float> fdl1Re, fdl1Im, fdl2Re, fdl2Im;
    std::vector<float> accRe, accIm;             // [set slot][ch][bin]
    std::vector<float> hOut, tOut;               // [set slot][ch][sample]
    std::vector<float> xfGain;                   // the crossfade curve
    IrFilter* set[2] { nullptr, nullptr };       // set[s0]: in use; set[s1]: coming in
    IrFilter* waiting = nullptr;
    IrFilter* retired = nullptr;
    int s0 = 0, s1 = 1;
    State state = Idle;
    int maxP2 = 1;
    int hFill = 0, tFill = 0, hRead = 0, tRead = 0, tdW = 0, xfPos = 0;
    int newest1 = 0, newest2 = 0;                // newest input spectrum in each delay line
    int done[2] { 1, 1 };                        // tail partitions accumulated so far, per set slot
    bool accOn[2] { false, false };              // which set slots accumulate for the next tail block
};
} // namespace sf
