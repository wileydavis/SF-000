#include "Convolver.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sf
{
namespace
{
constexpr int kB1 = IrFilter::kB1, kB2 = IrFilter::kB2, kP1 = IrFilter::kP1, kT1 = IrFilter::kT1;
constexpr int kN1 = IrFilter::kN1, kN2 = IrFilter::kN2, kBins1 = IrFilter::kBins1, kBins2 = IrFilter::kBins2;
constexpr int kDelayMask = 1023;                       // tail input delay line (kB1 < 1024)

inline void split (const float* w, float* re, float* im, int bins)
{
    for (int k = 0; k < bins; ++k) { re[k] = w[2 * k]; im[k] = w[2 * k + 1]; }
}
inline void join (float* w, const float* re, const float* im, int bins)
{
    for (int k = 0; k < bins; ++k) { w[2 * k] = re[k]; w[2 * k + 1] = im[k]; }
}
// acc += x * h (complex, split form)
inline void cmac (float* accRe, float* accIm, const float* xr, const float* xi, const float* hr, const float* hi, int n)
{
    for (int k = 0; k < n; ++k)
    {
        const float a = xr[k], b = xi[k], c = hr[k], d = hi[k];
        accRe[k] += a * c - b * d;
        accIm[k] += a * d + b * c;
    }
}
} // namespace

std::unique_ptr<IrFilter> makeIrFilter (const float* irL, const float* irR, int taps, int capacity)
{
    auto f = std::make_unique<IrFilter>();
    taps = std::max (0, taps);
    f->taps = taps;
    f->p1 = IrFilter::headParts (taps);
    f->p2 = IrFilter::tailParts (taps);
    f->cap2 = std::max (f->p2, IrFilter::tailParts (capacity));
    f->h1re.assign ((size_t) 2 * kP1 * kBins1, 0.0f); f->h1im.assign (f->h1re.size(), 0.0f);
    f->h2re.assign ((size_t) 2 * (size_t) f->cap2 * kBins2, 0.0f); f->h2im.assign (f->h2re.size(), 0.0f);
    juce::dsp::FFT fft1 (10), fft2 (13);
    std::vector<float> w ((size_t) 2 * kN2);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = ch == 0 ? irL : irR;
        for (int p = 0; p < f->p1; ++p)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            const int start = p * kB1, len = std::min (kB1, taps - start);
            std::copy (src + start, src + start + len, w.begin());
            fft1.performRealOnlyForwardTransform (w.data(), true);
            split (w.data(), f->h1re.data() + ((size_t) ch * kP1 + (size_t) p) * kBins1, f->h1im.data() + ((size_t) ch * kP1 + (size_t) p) * kBins1, kBins1);
        }
        for (int p = 0; p < f->p2; ++p)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            const int start = kT1 + p * kB2, len = std::min (kB2, taps - start);
            std::copy (src + start, src + start + len, w.begin());
            fft2.performRealOnlyForwardTransform (w.data(), true);
            const size_t at = ((size_t) ch * (size_t) f->cap2 + (size_t) p) * kBins2;
            split (w.data(), f->h2re.data() + at, f->h2im.data() + at, kBins2);
        }
    }
    return f;
}

void Convolver::transform (IrFilter& f, int ch, bool tail, int p, const float* x)
{
    // As makeIrFilter: the partition's samples, zero-padded to twice the block, transformed.
    const int b = tail ? kB2 : kB1, nfft = tail ? kN2 : kN1;
    std::fill (work.begin(), work.begin() + 2 * nfft, 0.0f);
    std::copy (x, x + b, work.begin());
    (tail ? fft2 : fft1)->performRealOnlyForwardTransform (work.data(), true);
    if (tail) { const size_t at = ((size_t) ch * (size_t) f.cap2 + (size_t) p) * kBins2; split (work.data(), f.h2re.data() + at, f.h2im.data() + at, kBins2); }
    else { const size_t at = ((size_t) ch * kP1 + (size_t) p) * kBins1; split (work.data(), f.h1re.data() + at, f.h1im.data() + at, kBins1); }
}

void Convolver::prepare (int maxTapsWanted)
{
    if (fft1 == nullptr) { fft1 = std::make_unique<juce::dsp::FFT> (10); fft2 = std::make_unique<juce::dsp::FFT> (13); }
    work.assign ((size_t) 2 * kN2, 0.0f);
    hIn.assign ((size_t) kN1, 0.0f);
    tIn.assign ((size_t) kN2, 0.0f);
    tDelay.assign ((size_t) kDelayMask + 1, 0.0f);
    maxP2 = std::max (1, (maxTapsWanted - kT1 + kB2 - 1) / kB2);
    fdl1Re.assign ((size_t) kP1 * kBins1, 0.0f); fdl1Im.assign (fdl1Re.size(), 0.0f);
    fdl2Re.assign ((size_t) maxP2 * kBins2, 0.0f); fdl2Im.assign (fdl2Re.size(), 0.0f);
    accRe.assign ((size_t) 4 * kBins2, 0.0f); accIm.assign (accRe.size(), 0.0f);
    hOut.assign ((size_t) 4 * kB1, 0.0f);
    tOut.assign ((size_t) 4 * kB2, 0.0f);
    xfGain.resize ((size_t) kB2);
    for (int j = 0; j < kB2; ++j) xfGain[(size_t) j] = (float) (0.5 - 0.5 * std::cos (3.14159265358979323846 * (j + 0.5) / kB2));
    reset();
}

void Convolver::reset()
{
    for (auto* v : { &hIn, &tIn, &tDelay, &fdl1Re, &fdl1Im, &fdl2Re, &fdl2Im, &accRe, &accIm, &hOut, &tOut })
        std::fill (v->begin(), v->end(), 0.0f);
    hFill = tFill = hRead = tRead = tdW = xfPos = 0;
    newest1 = newest2 = 0;
    if (state != Idle && set[s1] != nullptr)
    {
        // A swap in progress completes now: the new IR takes over, the old one is collected.
        if (retired == nullptr) retired = set[s0];
        set[s0] = nullptr;
        std::swap (s0, s1);
    }
    state = Idle;
    accOn[s0] = set[s0] != nullptr; accOn[s1] = false;
    done[0] = done[1] = 1;
}

std::vector<IrFilter*> Convolver::releaseAll()
{
    IrFilter* out[4];
    const int k = release (out);
    return std::vector<IrFilter*> (out, out + k);
}

int Convolver::release (IrFilter** out)
{
    int k = 0;
    for (auto* f : { set[0], set[1], waiting, retired }) if (f != nullptr) out[k++] = f;
    set[0] = set[1] = waiting = retired = nullptr;
    state = Idle;
    accOn[0] = accOn[1] = false;
    std::fill (hOut.begin(), hOut.end(), 0.0f);
    std::fill (tOut.begin(), tOut.end(), 0.0f);
    return k;
}

int Convolver::replaceNow (IrFilter* f, IrFilter** out)
{
    const int k = release (out);
    offer (f);                                   // the first IR: used at once
    return k;
}

bool Convolver::offer (IrFilter* f)
{
    if (f == nullptr) return true;
    if (set[s0] == nullptr && state == Idle && waiting == nullptr)
    {
        // The first IR: used at once. Its tail products for the current block catch up in spread().
        set[s0] = f;
        accOn[s0] = true; done[s0] = 1;
        std::fill (acc (s0, 0, false), acc (s0, 0, false) + kBins2, 0.0f); std::fill (acc (s0, 0, true), acc (s0, 0, true) + kBins2, 0.0f);
        std::fill (acc (s0, 1, false), acc (s0, 1, false) + kBins2, 0.0f); std::fill (acc (s0, 1, true), acc (s0, 1, true) + kBins2, 0.0f);
        return true;
    }
    if (busy()) return false;
    waiting = f;
    return true;
}

void Convolver::accumulate (int s, int upTo)
{
    const IrFilter* f = set[s];
    if (f == nullptr) return;
    upTo = std::min (upTo, f->p2);
    for (int p = done[s]; p < upTo; ++p)
    {
        const int k = ((newest2 - (p - 1)) % maxP2 + maxP2) % maxP2;
        for (int ch = 0; ch < 2; ++ch)
            cmac (acc (s, ch, false), acc (s, ch, true), fdl2 (k, false), fdl2 (k, true), f->tail (ch, p, false), f->tail (ch, p, true), kBins2);
    }
    done[s] = std::max (done[s], upTo);
}

void Convolver::spread()
{
    for (int s = 0; s < 2; ++s)
    {
        const IrFilter* f = set[s];
        if (! accOn[s] || f == nullptr || f->p2 <= 1) continue;
        const int target = 1 + (int) ((int64_t) tFill * (f->p2 - 1) / kB2);
        accumulate (s, target);
    }
}

void Convolver::tailBoundary()
{
    // The products still owed for the block about to start.
    for (int s = 0; s < 2; ++s)
        if (accOn[s] && set[s] != nullptr) accumulate (s, set[s]->p2);
    // The newest input frame into the delay line.
    newest2 = (newest2 + 1) % maxP2;
    std::copy (tIn.begin(), tIn.end(), work.begin());
    fft2->performRealOnlyForwardTransform (work.data(), true);
    split (work.data(), fdl2 (newest2, false), fdl2 (newest2, true), kBins2);
    std::copy (tIn.begin() + kB2, tIn.end(), tIn.begin());
    tFill = 0;
    tRead = 0;
    // Outputs for the next block: the IR in use; while preparing a swap, the new one too; during
    // the crossfade, only the new one (the block after it is the new IR's alone).
    const bool outS0 = state == Idle || state == Prep, outS1 = state == Prep || state == Xfade;
    for (int s = 0; s < 2; ++s)
    {
        float* o = tOut.data() + (size_t) s * 2 * kB2;
        const bool want = (s == s0 && outS0) || (s == s1 && outS1);
        const IrFilter* f = set[s];
        if (! want || f == nullptr || f->p2 == 0) { if (want || f == nullptr) std::fill (o, o + 2 * kB2, 0.0f); continue; }
        for (int ch = 0; ch < 2; ++ch)
        {
            float* ar = acc (s, ch, false); float* ai = acc (s, ch, true);
            cmac (ar, ai, fdl2 (newest2, false), fdl2 (newest2, true), f->tail (ch, 0, false), f->tail (ch, 0, true), kBins2);
            join (work.data(), ar, ai, kBins2);
            fft2->performRealOnlyInverseTransform (work.data());
            std::copy (work.begin() + kB2, work.begin() + kN2, o + (size_t) ch * kB2);
        }
    }
    // Swap steps.
    if (state == Prep) { state = Xfade; xfPos = 0; }
    else if (state == Xfade)
    {
        retired = set[s0];
        set[s0] = nullptr;
        std::swap (s0, s1);
        state = Idle;
    }
    if (state == Idle && waiting != nullptr && retired == nullptr && set[s0] != nullptr)
    {
        set[s1] = waiting;
        waiting = nullptr;
        state = Prep;
    }
    // What accumulates during the next block.
    accOn[s0] = set[s0] != nullptr && (state == Idle || state == Prep);
    accOn[s1] = set[s1] != nullptr && (state == Prep || state == Xfade);
    for (int s = 0; s < 2; ++s)
        if (accOn[s])
        {
            for (int ch = 0; ch < 2; ++ch) { std::fill (acc (s, ch, false), acc (s, ch, false) + kBins2, 0.0f); std::fill (acc (s, ch, true), acc (s, ch, true) + kBins2, 0.0f); }
            done[s] = 1;
        }
}

void Convolver::headBoundary()
{
    newest1 = (newest1 + 1) % kP1;
    std::copy (hIn.begin(), hIn.end(), work.begin());
    fft1->performRealOnlyForwardTransform (work.data(), true);
    split (work.data(), fdl1 (newest1, false), fdl1 (newest1, true), kBins1);
    std::copy (hIn.begin() + kB1, hIn.end(), hIn.begin());
    hFill = 0;
    hRead = 0;
    float yRe[kBins1], yIm[kBins1];
    for (int s = 0; s < 2; ++s)
    {
        float* o = hOut.data() + (size_t) s * 2 * kB1;
        const bool want = s == s0 || (s == s1 && state == Xfade);
        const IrFilter* f = set[s];
        if (! want || f == nullptr) { if (f == nullptr) std::fill (o, o + 2 * kB1, 0.0f); continue; }
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (yRe, yRe + kBins1, 0.0f); std::fill (yIm, yIm + kBins1, 0.0f);
            for (int p = 0; p < f->p1; ++p)
            {
                const int k = (newest1 - p + kP1) % kP1;
                cmac (yRe, yIm, fdl1 (k, false), fdl1 (k, true), f->head (ch, p, false), f->head (ch, p, true), kBins1);
            }
            join (work.data(), yRe, yIm, kBins1);
            fft1->performRealOnlyInverseTransform (work.data());
            std::copy (work.begin() + kB1, work.begin() + kN1, o + (size_t) ch * kB1);
        }
    }
}

void Convolver::process (const float* in, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const size_t h0 = (size_t) s0 * 2 * kB1, t0 = (size_t) s0 * 2 * kB2;
        float l = hOut[h0 + (size_t) hRead] + tOut[t0 + (size_t) tRead];
        float r = hOut[h0 + kB1 + (size_t) hRead] + tOut[t0 + kB2 + (size_t) tRead];
        if (state == Xfade)
        {
            const size_t h1 = (size_t) s1 * 2 * kB1, t1 = (size_t) s1 * 2 * kB2;
            const float a = xfGain[(size_t) std::min (xfPos, kB2 - 1)];
            ++xfPos;
            const float l1 = hOut[h1 + (size_t) hRead] + tOut[t1 + (size_t) tRead];
            const float r1 = hOut[h1 + kB1 + (size_t) hRead] + tOut[t1 + kB2 + (size_t) tRead];
            l += a * (l1 - l);
            r += a * (r1 - r);
        }
        outL[i] = l;
        outR[i] = r;
        ++hRead; ++tRead;
        const float x = in[i];
        hIn[(size_t) (kB1 + hFill++)] = x;
        tDelay[(size_t) tdW] = x;
        tdW = (tdW + 1) & kDelayMask;
        tIn[(size_t) (kB2 + tFill++)] = tDelay[(size_t) ((tdW - 1 - kB1) & kDelayMask)];
        if (tFill == kB2) tailBoundary();
        if (hFill == kB1) headBoundary();
    }
    spread();
}
} // namespace sf
