#pragma once
// POCKET: kick-aware ducking. Reads kick reports from the SF BUS (timing, velocity, pitch sweep,
// decay, filter) and carves a kick-shaped hole in this module's audio. Falls back to sidechain
// audio when no SF KICK is reporting. Adds a fixed lookahead (kLatency) so reports from the same
// audio cycle arrive before they are needed; the latency is reported to Live, which compensates.
// After an SF BUILD drop (DROP DUCK), every Pocket stage also ducks the whole band to each kick
// for a while, whatever its own DEPTH, so the kick lands on top of everything that comes back.
#include "Bus.h"
#include "Dsp.h"
#include "Module.h"
#include <atomic>

namespace sf
{
class Pocket
{
public:
    // SF-000 builds this as 0 (SF_POCKET_LATENCY): its engine plays the kick's lane first in every
    // block, so the kick's reports are there before any other lane needs them, with no wait.
   #ifndef SF_POCKET_LATENCY
    #define SF_POCKET_LATENCY 512
   #endif
    static constexpr int kLatency = SF_POCKET_LATENCY;
    static constexpr int kTrace = 128;
    // Takes part in SF BUILD's drop duck. Off on a TAP or STRIP whose ROLE is KICK, PERC or DECK:
    // like SF KICK, PERC, STRIKE and DECK, which have no Pocket, they never duck under the kick.
    bool dropDuck = true;

    void prepare (double rate, int maxBlock)
    {
        juce::ignoreUnused (maxBlock);
        sr = rate;
        for (auto& d : delay) d.prepare (kLatency + 8);
        sideDelay.prepare (kLatency + 8);
        sideLp.set (150.0f, 0.7071f, sr);
        sideFollow.set (0.0005f, 0.12f, sr);
        reset();
    }
    // Clears the lookahead line and the filters (PANIC). The latency stays the same.
    void reset()
    {
        for (auto& d : delay) d.clear();
        sideDelay.clear();
        sideLp.reset();
        sideFollow.z = 0.0f;
        for (auto& x : xo) x.reset();
        for (auto& b : bell) b.reset();
        presence = 0.0f; duty = 0.0f; envHold = 0.0f; sidePeak = 0.05f; act = 0.0f; dropDip = 1.0f;
    }

    // (SF-000) The pocket is the kick wrapped in the lane. The hole is made from the kick's own envelope as the kick
    // reports it (its decay): what the kick has left of the level is 1 less that envelope, and the lane's level is that,
    // squared: the lane is out through the kick's punch and comes up to meet its end. DEPTH is how deep the cut goes
    // (the whole lane's level, as a fade drawn on its track would be) and SHAPE is its length: the kick's curve scaled
    // in time, a quarter of the kick's length at nought, all of it at full. Neither stretched for the tempo nor
    // shortened when kicks come thick: it is the kick's. (Before the seventy-ninth build SHAPE went from a notch at the
    // kick's pitch, through a cut in the lows, to a pump of the whole lane, on a decay held to a part of a beat.)
    // depth 0..1, shape 0..1.
    // enabled=false still delays (latency must stay constant) but applies no gain change.
    void process (const Ctx& c, float* const* ch, int numCh, int n, float depth, float shape,
                  const juce::AudioBuffer<float>* side, bool enabled = true, bool preferSide = false, bool delayAudio = true)
    {
        auto& bus = *c.bus;
        KickHit hits[kKickRing];
        int numHits = bus.recentKicks (hits, kKickRing);
        const bool haveSide = side != nullptr && side->getNumChannels() > 0;
        const bool busKick = numHits > 0 && bus.kickAlive (4000) && ! (preferSide && haveSide);
        if (! busKick) numHits = 0;
        const bool useSide = ! busKick && haveSide;

        const double bps = c.beatsPerSample();
        const double secPerBeat = 60.0 / c.bpm;
        // POCKET OFFSET (SF CONTROL) shifts every pocket; + = later.
        const double lookBeats = kLatency * bps + (double) bus.d().pocketOffsetMs.load() * 0.001 / secPerBeat;
        const double lookSec = kLatency / sr;

        // Latest hit parameters for the band split frequency and kick filter influence.
        float f1 = 50.0f, kickHp = 0.0f;
        double newestPpq = -1e9;
        for (int h = 0; h < numHits; ++h)
            if (hits[h].ppq <= c.ppq - lookBeats + n * bps && hits[h].ppq > newestPpq) { newestPpq = hits[h].ppq; f1 = hits[h].f1; kickHp = hits[h].hp; }
        if (useSide) f1 = 55.0f;

        const float s = clamp01 (shape);
        const float bandAmt = clamp01 (depth);                 // (SF-000) the whole lane, as deep as DEPTH
        const float bellAmt = 0.0f;
        const float fcLow = std::max (40.0f, f1 * 3.0f);
        const float wrapLen = std::pow (4.0f, s - 1.0f);        // (SF-000) SHAPE: a quarter of the kick's length, to all of it
        // A high-passed kick has no sub to make room for: the pocket closes as the kick thins.
        float lowPresence = 1.0f;
        if (kickHp > 30.0f) lowPresence = clamp01 (std::pow (fcLow / kickHp, 2.0f));

        // SF BUILD's drop: full-band duck to each kick, dropDb deep, for dropBeats after the drop's
        // bar line (full for the first half, easing out over the second). Kick reports only: the
        // drop is musical time. Rejected after a jump back, like a stale kick report.
        const auto& bd = bus.d();
        float dropDb = bd.dropDb.load();
        const double dropAt = bd.dropPpq.load(), dropBeats = bd.dropBeats.load();
        if (! dropDuck || ! busKick || ! (dropDb > 0.01f) || dropAt < 0.0 || ! (dropBeats > 0.0)) dropDb = 0.0f;
        else
        {
            const double from = bd.dropFromPpq.load();
            const double played = (c.ppq - from) * secPerBeat, elapsed = Bus::nowSec() - bd.dropWall.load();
            if (c.ppq < from - 1e-6 || elapsed > played + lookSec + 0.35 || c.ppq - lookBeats > dropAt + dropBeats) dropDb = 0.0f;
        }
        dropDb = std::min (dropDb, 24.0f);
        if (dropDb == 0.0f) dropDip = 1.0f;
        // The drop's kicks: from its bar line, or up to 10 ms early (MIDI timing). A roll before
        // the drop doesn't count, and the duck fades in over 5 ms from the bar line: never a step.
        const double dropEarly = 0.010 / secPerBeat, dropFade = 0.005 / secPerBeat;

        // The band split (an all-pass when summed back) only runs while the band duck is in use;
        // otherwise the audio passes untouched (crossfaded over 10 ms when that changes).
        const float actTarget = enabled && bandAmt > 0.0005f ? 1.0f : 0.0f;
        const float actCoef = std::exp (-1.0f / (float) (0.01 * sr));
        const float holdCoef = std::exp (-1.0f / (float) (0.001 * sr));        // (SF-000) the curve is the kick's: nothing holds it on
        const float dutyCoef = std::exp (-1.0f / (float) (2.0 * secPerBeat * sr));
        const float presUp = (float) (1.0 / (secPerBeat * sr)), presDown = (float) (1.0 / (8.0 * secPerBeat * sr));

        for (int i = 0; i < n; ++i)
        {
            const double tq = c.ppqAt (i) - lookBeats;    // musical time of the sample leaving now
            float env = 0.0f, pitch = f1, kickEnv = 0.0f;
            bool anyRecent = false;
            if (busKick)
            {
                double best = -1e9;
                for (int h = 0; h < numHits; ++h)
                {
                    const double dtBeats = tq - hits[h].ppq;
                    if (dtBeats < 0.0 || dtBeats > 8.0) continue;
                    if (! Bus::kickCurrent (hits[h], dtBeats, secPerBeat, lookSec)) continue;   // left over from before a jump
                    anyRecent = anyRecent || dtBeats < 8.0;
                    const float dt = (float) (dtBeats * secPerBeat);
                    // (SF-000) what the kick has left of the level: nothing on the hit, all of it when the kick is gone
                    const float att = std::min (1.0f, dt / 0.002f);
                    const float room = 1.0f - std::exp (-dt / std::max (0.01f, hits[h].decay * wrapLen));
                    const float e = hits[h].vel * att * (1.0f - room * room);
                    env = std::max (env, e);
                    if (dropDb > 0.0f && hits[h].ppq >= dropAt - dropEarly)
                    {
                        // The drop duck follows the kick itself: back within half a beat at most.
                        const float decK = std::max (0.05f, std::min (hits[h].decay, 0.5f * (float) secPerBeat));
                        kickEnv = std::max (kickEnv, juce::jlimit (0.0f, 1.0f, hits[h].vel) * att * std::exp (-dt / decK));
                    }
                    if (hits[h].ppq > best)
                    {
                        best = hits[h].ppq;
                        pitch = hits[h].f1 + (hits[h].f0 - hits[h].f1) * std::exp (-dt / std::max (0.001f, hits[h].tau));
                    }
                }
            }
            else if (useSide)
            {
                float x = 0.0f;
                for (int sc = 0; sc < side->getNumChannels(); ++sc) x += side->getReadPointer (sc)[i];
                const float f = sideFollow.tick (sideLp.tickLP (x));
                sidePeak = std::max (f, sidePeak * 0.99999f);
                sideDelay.push (f / std::max (0.01f, sidePeak));
                env = sideDelay.tap (kLatency);
                anyRecent = env > 0.05f;
            }

            // Kick returns after a gap: fade the pocket back in over a beat.
            presence = anyRecent ? std::min (1.0f, presence + presUp) : std::max (0.0f, presence - presDown);
            envHold = std::max (env, envHold * holdCoef);
            duty = envHold + dutyCoef * (duty - envHold);
            const float e = enabled ? envHold * presence : 0.0f;

            if (((c.samplePos + i) & 15) == 0)          // on the stream grid: block-size independent
            {
                const float g = -18.0f * bellAmt * e * lowPresence;
                for (int ci = 0; ci < 2; ++ci) bell[ci].peak (std::max (30.0f, pitch), 2.2f, g, sr);
            }
            const float lowGain = 1.0f - bandAmt * e * lowPresence;
            float dropGain = 1.0f;
            if (dropDb > 0.0f)
            {
                const double x = (tq - dropAt) / dropBeats;
                if (x >= 0.0 && x < 1.0)
                {
                    if (((c.samplePos + i) & 15) == 0 || dropDip >= 1.0f)
                    {
                        const float w = x < 0.5 ? 1.0f : (float) (0.5 + 0.5 * std::cos (6.283185307179586 * (x - 0.5)));
                        dropDip = dbToGain (-dropDb * w);
                    }
                    const float fadeIn = (float) std::min (1.0, (tq - dropAt) / dropFade);
                    dropGain = 1.0f - (1.0f - dropDip) * kickEnv * fadeIn;
                }
                else dropDip = 1.0f;
            }
            act = actTarget + actCoef * (act - actTarget);
            if (act < 1.0e-4f && actTarget == 0.0f) act = 0.0f;
            for (int ci = 0; ci < std::min (numCh, 2); ++ci)
            {
                float x = ch[ci][i];
                if (delayAudio) { delay[ci].push (x); x = delay[ci].tap (kLatency); }
                if (act > 0.0f) x = x + act * (x * lowGain - x);          // (SF-000) the lane's level
                ch[ci][i] = bell[ci].tick (x) * dropGain;
            }
            traceAcc = std::max ({ traceAcc, e * std::max (bandAmt, bellAmt), 1.0f - dropGain });
            if (++traceCount >= 256)
            {
                trace[(size_t) traceWrite].store (traceAcc);
                traceWrite = (traceWrite + 1) % kTrace;
                traceAcc = 0.0f; traceCount = 0;
            }
        }
        sourceIsBus = busKick;
        sourceIsSide = useSide;
    }

    void paint (juce::Graphics& g, juce::Rectangle<float> r) const;

    std::atomic<float> trace[kTrace] {};
    int traceWrite = 0;
    std::atomic<bool> sourceIsBus { false }, sourceIsSide { false };

private:
    double sr = 48000.0;
    DelayLine delay[2], sideDelay;
    Crossover xo[2];
    Biquad bell[2];
    Svf sideLp;
    Follower sideFollow;
    float presence = 0, duty = 0, envHold = 0, sidePeak = 0.05f, traceAcc = 0, act = 0, dropDip = 1.0f;
    int traceCount = 0;
};
} // namespace sf
