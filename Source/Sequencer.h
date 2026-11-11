#pragma once
// SF-000 sequencer. One pattern per lane, 1 to 128 steps of a sixteenth. Every step holds a
// velocity, a gate (in steps), a ratchet count and, on a pitched lane, a note. A gate of 1 is the
// whole step and stops there. Dragged past the step's edge it runs on: through unlit steps as a held
// note, and into the next lit step as legato (the notes overlap: a slide on ACID, a glide on the
// mono models); onto the same note it is a tie (one note, held, no new attack).
// The audio thread turns a pattern into MIDI on the sample.
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sf0
{
constexpr int kMaxSteps = 128;
constexpr double kStepBeats = 0.25;      // a sixteenth
constexpr float kLegatoOver = 0.05f;     // how far past the next lit step a gate must reach to be legato into it

struct Step
{
    bool on = false;
    float vel = 0.75f;      // 0..1; high is the accent on ACID
    float gate = 1.0f;      // steps: 1 is the whole step
    int ratchet = 1;        // 1..4 hits in the step
    int note = 0;           // 0..11, C..B
    int octave = 3;         // C3 = MIDI 60, as the suite counts
};

// A step put out forgets everything it held.
inline void clearStep (Step& s, int octave) { s = Step(); s.octave = octave; }

struct Pattern
{
    std::array<Step, kMaxSteps> steps {};
    int length = 16;
};

// ---------------------------------------------------------------------------------------------
// Generators. Seeded: the same knobs always give the same pattern. Rhythm only: a step keeps the
// note it holds.
struct GenParams
{
    int type = -1;          // -1 none, 0 RANDOM, 1 EUCLID (the knobs are armed while >= 0)
    float hits = 0.31f, rotate = 0.5f, accents = 0.2f, seed = 0.0f;   // knob positions 0..1
};

inline float seqHash (int a, int b)
{
    uint32_t h = (uint32_t) (a + 1) * 374761393u + (uint32_t) (b + 1) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((double) (h ^ (h >> 16)) / 4294967296.0);
}

inline void generate (Pattern& p, const GenParams& g)
{
    const int len = juce::jlimit (1, kMaxSteps, p.length);
    const int H = juce::jlimit (0, len, (int) std::lround (g.hits * (float) len));
    const int R = (int) std::lround ((g.rotate - 0.5f) * 16.0f);
    const int seed = (int) std::lround (g.seed * 15.0f);
    const int A = juce::jlimit (0, H, (int) std::lround (g.accents * (float) H));
    bool hit[kMaxSteps] = {}, acc[kMaxSteps] = {};
    if (g.type == 1)
    {
        int h = 0;
        for (int i = 0; i < len; ++i)
            if (H > 0 && (i * H) % len < H) { hit[i] = true; acc[i] = A > 0 && (h * A) % H < A; ++h; }
    }
    else
    {
        // Every step has a seeded rank, leaning to the beat; HITS takes the top of the ranking, so
        // turning it adds and removes hits without moving the others.
        std::array<std::pair<float, int>, kMaxSteps> order;
        for (int i = 0; i < len; ++i)
            order[(size_t) i] = { seqHash (seed, i) * 0.7f + (i % 4 == 0 ? 0.3f : i % 2 == 0 ? 0.15f : 0.05f), i };
        std::stable_sort (order.begin(), order.begin() + len, [] (auto& a, auto& b) { return a.first > b.first; });
        std::array<std::pair<float, int>, kMaxSteps> hits;
        for (int k = 0; k < H; ++k) { const int i = order[(size_t) k].second; hit[i] = true; hits[(size_t) k] = { seqHash (seed + 97, i), i }; }
        std::stable_sort (hits.begin(), hits.begin() + H, [] (auto& a, auto& b) { return a.first > b.first; });
        for (int k = 0; k < A; ++k) acc[hits[(size_t) k].second] = true;
    }
    for (int i = 0; i < len; ++i) { auto& s = p.steps[(size_t) i]; s.on = false; s.vel = 0.75f; s.gate = 1.0f; s.ratchet = 1; }
    for (int i = 0; i < len; ++i)
        if (hit[i])
        {
            auto& s = p.steps[(size_t) ((((i + R) % len) + len) % len)];
            s.on = true; s.vel = acc[i] ? 1.0f : 0.75f;
        }
}

// The whole pattern one step earlier (-1) or later (+1), notes and all.
inline void nudge (Pattern& p, int dir)
{
    const int len = juce::jlimit (1, kMaxSteps, p.length);
    std::array<Step, kMaxSteps> head = p.steps;
    for (int i = 0; i < len; ++i) p.steps[(size_t) ((((i + dir) % len) + len) % len)] = head[(size_t) i];
}

// Steps from i to the next lit step, going round (the pattern's length if i is the only one).
inline int stepsToNext (const Pattern& p, int i)
{
    const int len = juce::jlimit (1, kMaxSteps, p.length);
    for (int d = 1; d < len; ++d) if (p.steps[(size_t) ((i + d) % len)].on) return d;
    return len;
}

// ---------------------------------------------------------------------------------------------
// One lane's player (audio thread). Events are kept by song position and sent on their sample.
struct LanePlayer
{
    Pattern pat;
    bool pitched = false;
    int drumNote = 60;

    struct Ev { double ppq = 0; int note = 0, vel = 0; bool on = false, live = false, tieable = false; };
    std::array<Ev, 64> q {};

    void push (double ppq, int note, int vel, bool on, bool tieable = false)
    {
        for (auto& e : q) if (! e.live) { e = { ppq, note, vel, on, true, tieable }; return; }
    }

    // Every sounding note off now; nothing left waiting.
    void stop (juce::MidiBuffer& out, int sample)
    {
        for (auto& e : q)
        {
            if (e.live && ! e.on) out.addEvent (juce::MidiMessage::noteOff (1, e.note), sample);
            e.live = false;
        }
    }

    // The events of [q0, q1), in a block of n samples at spb samples a beat that starts at block
    // (q0 itself, unless the block is rendered in two parts round a bar line).
    void render (double q0, double q1, double spb, int n, juce::MidiBuffer& out, double block)
    {
        const int len = juce::jlimit (1, kMaxSteps, pat.length);
        const auto first = (int64_t) std::ceil (q0 / kStepBeats - 1e-9);
        for (int64_t k = first; (double) k * kStepBeats < q1 - 1e-12; ++k)
        {
            if (k < 0) continue;
            const double tq = (double) k * kStepBeats;
            const int i = (int) (k % len);
            const Step& st = pat.steps[(size_t) i];
            if (! st.on) continue;
            const int note = pitched ? juce::jlimit (0, 127, 12 * (st.octave + 2) + st.note) : drumNote;
            const int vel = juce::jlimit (1, 127, (int) std::lround (st.vel * 127.0f));
            const int r = juce::jlimit (1, 4, st.ratchet);
            if (r > 1)
            {
                const double sub = kStepBeats / r, g = juce::jlimit (0.05, 1.0, (double) st.gate) * sub;
                for (int j = 0; j < r; ++j) { push (tq + j * sub, note, vel, true); push (tq + j * sub + g, note, 0, false); }
                continue;
            }
            const int dist = stepsToNext (pat, i);
            // past the next lit step's edge: legato into it. Up to the edge: the note ends there and the next one strikes.
            const bool reaches = st.gate >= (float) dist + kLegatoOver;
            const double end = reaches ? tq + (dist + 0.12) * kStepBeats
                                       : tq + juce::jlimit (0.05, (double) dist, (double) st.gate) * kStepBeats;
            // A tie: the note before reached this step and is the same note: it carries on.
            bool tied = false;
            for (auto& e : q)
                if (e.live && ! e.on && e.tieable && e.note == note && e.ppq > tq) { e.ppq = end; e.tieable = reaches; tied = true; break; }
            if (tied) continue;
            push (tq, note, vel, true);
            push (end, note, 0, false, reaches);
        }
        // Send what is due, offs before ons on the same sample.
        for (int pass = 0; pass < 2; ++pass)
            for (;;)
            {
                Ev* best = nullptr;
                for (auto& e : q) if (e.live && e.on == (pass == 1) && e.ppq < q1 && (best == nullptr || e.ppq < best->ppq)) best = &e;
                if (best == nullptr) break;
                const int s = juce::jlimit (0, std::max (0, n - 1), (int) std::floor ((best->ppq - block) * spb + 1e-6));
                if (best->on) out.addEvent (juce::MidiMessage::noteOn (1, best->note, (juce::uint8) best->vel), s);
                else          out.addEvent (juce::MidiMessage::noteOff (1, best->note), s);
                // a sent off stays known until its time has passed, so a tie can still find it
                best->live = false;
            }
    }
};
} // namespace sf0
