// SF-000: TRANSITION. Two one-shots timed to a bar line: a LEAD-IN that ends on it (a riser) and a HIT that
// starts on it (a stab, an impact). Fired by hand, some time before the line; the machine keeps the time, so
// the lead-in ends and the hit lands on the sample the line falls on. Each is a built-in sound or a file.
// They go into the mix with the returns, before the master, and by two amounts into the returns themselves.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include "core/Sample.h"

namespace sf0
{
struct Transition
{
    enum Kind { kOff = 0, kBuilt = 1, kFile = 2 };

    // ---- said by the message thread
    std::atomic<int> leadKind { kBuilt }, hitKind { kBuilt };
    std::atomic<const sf::SampleData*> leadSmp { nullptr }, hitSmp { nullptr };      // (kept alive by the engine for as long as it lives)
    std::atomic<float> level { 0.5f }, toA { 0.0f }, toB { 0.0f };
    // a firing: counted, with where it was asked (from) and the line it lands on (at), in beats of the song;
    // or, with the clock stopped, the hit at once
    std::atomic<int> askSeq { 0 };
    std::atomic<double> askFrom { 0.0 }, askAt { 0.0 };
    std::atomic<bool> askNow { false };
    // (what this firing plays, if not what the two slots say: -1 the slot's; kOff; or kFile with the file here)
    std::atomic<int> askLeadKind { -1 }, askHitKind { -1 };
    std::atomic<const sf::SampleData*> askLead { nullptr }, askHit { nullptr };
    // ---- said by the audio thread
    std::atomic<bool> busy { false };           // a line is waited for, or a hit is sounding
    std::atomic<bool> armed { false };          // (the session's ARM, for the MIDI thread: a mute or a solo that arrives there waits too)

    void prepare (double rate) { sr = rate; waiting = leadOn = hitOn = false; seen = askSeq.load(); busy = false; }

    // Adds this block's sound to l and r (n samples; the block starts at ppq0 and is that many beats long). False: silence.
    bool render (float* l, float* r, int n, bool run, double ppq0, double beats) noexcept
    {
        if (const int s = askSeq.load(); s != seen)
        {
            seen = s;
            // what this firing plays, taken once: the slots as they stand now, or its own
            { const int k = askLeadKind.load(); curLeadKind = k >= 0 ? k : leadKind.load(); curLead = k == kFile ? askLead.load() : leadSmp.load(); }
            { const int k = askHitKind.load(); curHitKind = k >= 0 ? k : hitKind.load(); curHit = k == kFile ? askHit.load() : hitSmp.load(); }
            if (askNow.load()) { waiting = false; leadOn = false; startHit(); }
            else
            {
                from = askFrom.load(); at = askAt.load();
                waiting = at > from;
                leadOn = waiting && curLeadKind != kOff;
                leadPos = -1.0; leadFade = 1.0f;
                for (auto& ch : svf) ch[0] = ch[1] = 0.0f;
            }
        }
        if (waiting && (! run || ppq0 > at + 1.0)) { waiting = false; leadOn = false; }        // the clock stopped, or went somewhere else: called off
        busy = waiting || hitOn;
        if (! waiting && ! hitOn) return false;

        const double spb = beats > 1.0e-9 ? (double) n / beats : sr * 60.0 / 132.0;             // samples in a beat
        int hitAt = -1;                                                                         // the sample of this block the line falls on
        if (waiting && ppq0 + beats >= at) hitAt = juce::jlimit (0, n - 1, (int) std::llround ((at - ppq0) * spb));
        const float g = level.load();
        const int lk = curLeadKind;
        const sf::SampleData* const ls = lk == kFile ? curLead : nullptr;
        const sf::SampleData* const hs = curHitKind == kFile ? curHit : nullptr;
        // the riser, as far along as this block starts: its band rises from 180 Hz to 9 kHz and it grows as the square of the way
        const float x0 = waiting ? (float) juce::jlimit (0.0, 1.0, (ppq0 - from) / (at - from)) : 0.0f;
        const float fc = 180.0f * std::pow (50.0f, x0);
        const float gs = std::tan (juce::MathConstants<float>::pi * std::min (fc, (float) sr * 0.45f) / (float) sr), ks = 0.7f;
        const float a1 = 1.0f / (1.0f + gs * (gs + ks)), a2 = gs * a1, a3 = gs * a2;
        const float dx = waiting && n > 0 ? (float) (beats / (at - from) / (double) n) : 0.0f;
        float x = x0;

        for (int i = 0; i < n; ++i)
        {
            if (i == hitAt) { waiting = false; leadOn = false; startHit(); }
            float yl = 0.0f, yr = 0.0f;
            if (waiting && leadOn)
            {
                if (lk == kBuilt)
                {
                    const float amp = x * x * 0.7f;
                    const float in[2] = { noise(), noise() };
                    float out[2];
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        // (a band-pass, state variable: the band is the middle of it)
                        const float v3 = in[ch] - svf[ch][1], v1 = a1 * svf[ch][0] + a2 * v3, v2 = svf[ch][1] + a2 * svf[ch][0] + a3 * v3;
                        svf[ch][0] = 2.0f * v1 - svf[ch][0]; svf[ch][1] = 2.0f * v2 - svf[ch][1];
                        out[ch] = v1 * amp;
                    }
                    yl = out[0]; yr = out[1];
                    x += dx;
                }
                else if (ls != nullptr)
                {
                    // a file: it starts where it must to end on the line (part way in, faded up, if there is less time than it is long)
                    // (step: the file's samples in one of the engine's, should the engine's rate have changed since it was read)
                    const double len = (double) ls->length(), step = ls->sr / sr;
                    if (leadPos < 0.0)
                    {
                        const double left = (at - (ppq0 + (double) i / spb)) * spb * step;      // (the time left, in the file's samples)
                        if (left <= len) { leadPos = std::max (0.0, len - left); leadFade = leadPos > 64.0 ? 0.0f : 1.0f; }
                    }
                    if (leadPos >= 0.0 && leadPos + 2.0 < len)
                    {
                        yl = ls->read (0, leadPos, step) * leadFade; yr = ls->read (1, leadPos, step) * leadFade;
                        leadPos += step;
                        leadFade = std::min (1.0f, leadFade + 1.0f / (0.01f * (float) sr));
                    }
                }
            }
            if (hitOn)
            {
                if (hs != nullptr)
                {
                    const double step = hs->sr / sr;
                    if (hitPos + 2.0 < (double) hs->length()) { yl += hs->read (0, hitPos, step); yr += hs->read (1, hitPos, step); hitPos += step; }
                    else hitOn = false;
                }
                else if (hitBuilt)
                {
                    // the built-in impact: a sine that falls from 120 Hz to 42 and dies over a second, under a burst of dark noise
                    const float t = (float) (hitPos / sr);
                    hitPh += (42.0f + 80.0f * std::exp (-t / 0.09f)) / (float) sr; if (hitPh >= 1.0f) hitPh -= 1.0f;
                    hitLp += 0.28f * (noise() - hitLp);
                    const float y = std::sin (juce::MathConstants<float>::twoPi * hitPh) * std::exp (-t / 0.7f) * 0.5f * std::min (1.0f, t / 0.002f)
                                    + hitLp * std::exp (-t / 0.16f) * 0.4f;
                    yl += y; yr += y;
                    hitPos += 1.0;
                    if (t > 4.0f) hitOn = false;
                }
                else hitOn = false;
            }
            l[i] += yl * g; r[i] += yr * g;
        }
        busy = waiting || hitOn;
        return true;
    }

private:
    void startHit() noexcept
    {
        const int k = curHitKind;
        hitBuilt = k == kBuilt;
        hitOn = k == kBuilt || (k == kFile && curHit != nullptr);
        hitPos = 0.0; hitPh = 0.0f; hitLp = 0.0f;
    }
    float noise() noexcept { rng = rng * 1664525u + 1013904223u; return (float) (juce::int32) rng * (1.0f / 2147483648.0f); }

    double sr = 48000.0;
    int seen = 0;
    bool waiting = false, leadOn = false, hitOn = false, hitBuilt = true;
    double from = 0.0, at = 0.0, leadPos = -1.0, hitPos = 0.0;
    float leadFade = 1.0f, hitPh = 0.0f, hitLp = 0.0f;
    float svf[2][2] {};
    int curLeadKind = kBuilt, curHitKind = kBuilt;              // (this firing's)
    const sf::SampleData* curLead = nullptr; const sf::SampleData* curHit = nullptr;
    juce::uint32 rng = 0x2545F491u;
};
} // namespace sf0
