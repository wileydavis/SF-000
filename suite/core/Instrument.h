#pragma once
// Shared instrument plumbing: MIDI parsing, DENSITY echoes (ghost notes) and GATE length.
// DENSITY fills a sparse pattern with quieter, darker, shorter repeats of each note:
//   0-50%  : one echo one ECHO SPACE later (1/8 hats become 1/16 hats)
//   50-100%: two more ghosts at half spacing (1/32 rolls)
// Echo generation (gen) is passed to the voice: each generation is darker and shorter.
// A ghost carries two levels: `vel` (played softer: the instrument's velocity response, accent,
// timbre) and `gain` (the DENSITY fade-in, applied by the instrument after its velocity response,
// so a ghost really rises from silence: at DENSITY 1 % it is 34 dB down, not just a softer hit).
#include "Dsp.h"
#include "Module.h"

namespace sf
{
struct NoteEvent
{
    int offset = 0;     // sample in block
    int note = 60;
    float vel = 1.0f;   // 0..1
    int gen = 0;        // 0 = played note, 1.. = echoes
    bool on = true;
    float gain = 1.0f;  // DENSITY fade (post velocity response); 1 for played notes
};

class EchoScheduler
{
public:
    void reset() { count = 0; now = 0; }

    // Collect this block's played notes + due echoes into `out` (sorted by offset).
    // density 0..1, spaceBeats = echo spacing, swing 0..1, prob 0..1 (chance each echo plays)
    // `out` must have capacity reserved in prepare(); run() never grows it (audio thread).
    void run (const Ctx& c, juce::MidiBuffer& midi, std::vector<NoteEvent>& out,
              float density, double spaceBeats, float swing, float prob, bool echoNoteOffs = true)
    {
        out.clear();
        const double spb = c.samplesPerBeat();
        const double space = spaceBeats * spb;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int off = juce::jlimit (0, std::max (0, c.n - 1), meta.samplePosition);
            if (m.isNoteOn())
            {
                NoteEvent e { off, m.getNoteNumber(), m.getFloatVelocity(), 0, true };
                if (out.size() < out.capacity()) out.push_back (e);
                schedule (e, off, density, space, swing, prob, echoNoteOffs);
            }
            else if (m.isNoteOff() && out.size() < out.capacity())
                out.push_back ({ off, m.getNoteNumber(), 0.0f, 0, false });
            else if ((m.isAllNotesOff() || m.isAllSoundOff()) && out.size() < out.capacity())
                out.push_back ({ off, -1, 0.0f, 0, false });
        }
        // Due echoes.
        for (int i = 0; i < count;)
        {
            auto& p = pending[i];
            const int64_t rel = p.when - now;
            if (rel < c.n)
            {
                NoteEvent e = p.ev;
                e.offset = (int) std::max<int64_t> (0, rel);
                if (out.size() < out.capacity()) out.push_back (e);
                pending[i] = pending[--count];
            }
            else ++i;
        }
        // Stable insertion sort by offset (a handful of events; std::stable_sort may allocate).
        for (size_t a = 1; a < out.size(); ++a)
        {
            const NoteEvent e = out[a];
            size_t b = a;
            while (b > 0 && out[b - 1].offset > e.offset) { out[b] = out[b - 1]; --b; }
            out[b] = e;
        }
        now += c.n;
    }

private:
    struct Pending { int64_t when; NoteEvent ev; };
    static constexpr int kMax = 512;
    Pending pending[kMax];
    int count = 0;
    int64_t now = 0;
    Noise rng;

    void add (int64_t when, const NoteEvent& e)
    {
        if (count < kMax) pending[count++] = { when, e };
    }

    void schedule (const NoteEvent& e, int off, float d, double space, float swing, float prob, bool offs)
    {
        if (d <= 0.001f || space < 8.0) return;
        const int64_t base = now + off;
        const float t1 = clamp01 (d * 2.0f);             // first echo fades in over 0..50%
        const float t2 = clamp01 ((d - 0.5f) * 2.0f);    // ghosts fade in over 50..100%
        auto emit = [&] (double at, float fade, float softer, int gen)
        {
            if (fade <= 0.01f || rng.uni() > prob) return;
            NoteEvent g = e;
            g.vel = e.vel * softer;
            g.gain = fade;
            g.gen = gen;
            add (base + (int64_t) at, g);
            if (offs) add (base + (int64_t) (at + space * 0.45), { 0, e.note, 0.0f, gen, false });
        };
        const double sw = swing * space / 3.0;
        emit (space + sw, t1, 0.62f, 1);
        emit (space * 0.5, t2, 0.38f, 2);
        emit (space * 1.5 + sw, t2, 0.32f, 2);
    }
};

// GATE: 0 = clipped staccato, 0.5 = as played, 1 = held long.
// Returns the auto note-off time in samples (or -1 = follow the note) and a release multiplier.
inline void gateShape (float gate, double spb, int& autoOffSamples, float& releaseMul)
{
    if (gate < 0.5f)
    {
        const double beats = 0.03 + (gate / 0.5) * 0.47;    // 1/32-ish .. 1/2 beat
        autoOffSamples = (int) (beats * spb);
        releaseMul = 0.5f + gate;
    }
    else
    {
        autoOffSamples = -1;
        releaseMul = 1.0f + (gate - 0.5f) * 2.0f * 7.0f;    // up to 8x release
    }
}

// Echo spacing menu shared by instruments.
inline std::vector<std::string> echoSpaceNames() { return { "1/8", "1/16", "1/16T", "1/32", "1/8T", "1/8D" }; }
inline double echoSpaceBeats (int i)
{
    static const double b[] = { 0.5, 0.25, 1.0 / 6.0, 0.125, 1.0 / 3.0, 0.75 };
    return b[juce::jlimit (0, 5, i)];
}
} // namespace sf
