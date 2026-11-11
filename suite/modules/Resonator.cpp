// SF RESONATOR [Nov 10]: a bank of six tuned resonators (feedback combs, each a string's harmonic
// series) on a chord in the set's key. Whatever goes in (hats, a clap, noise, a stab, the room)
// rings on the chord: dub techno's metallic drones and chords out of percussion.
//   The chord: CHORD's notes from the root up, then again an octave higher, six in all (OCTAVES: three).
//   The root: SF CONTROL's KEY when it is set and KEY FOLLOW is on, else ROOT; OCTAVE puts it at
//   C1-C4, SHIFT moves everything by semitones. A change of chord, key or shift glides the
//   resonators there (30 ms): a slide, never a click.
//   Each resonator: a delay of one period with a one-pole low-pass in its loop (DAMP: bright metal
//   to dark wood), the loop's delay corrected for that low-pass's phase at the note, so it rings in
//   tune at any DAMP; feedback for DECAY (time to -60 dB, the same for every note); a soft limit in
//   the loop (DRIVE: clean to gritty; always there, out of the way at normal levels, so the loop
//   can never run away). The input goes in DC-free and scaled for each resonator so that white noise
//   comes out as loud as it went in, whatever DECAY and DAMP are: by 1 / sqrt of the loop's mean power
//   gain over the band, 1 + g^2 a^2 / sqrt(c^2 - 4 b^2) (b = 1 - a, c = 1 + b^2 - g^2 a^2, a the
//   low-pass's coefficient), and by 1 / sqrt(6) for the six together.
//   TONE: the input's DJ filter (left darker, right brighter), what the strings are struck with.
//   SPREAD: the resonators across the stereo field, alternately. FREEZE: over 30 ms the input fades
//   out, the loops' low-pass opens and DECAY becomes ten minutes: the ringing stops dying, as it is.
#include "Kit.h"

namespace sf
{
namespace
{
const std::vector<std::vector<int>>& resonatorChords()
{
    static const std::vector<std::vector<int>> t { { 0 }, { 0, 7 }, { 0, 3, 7 }, { 0, 3, 7, 10 }, { 0, 3, 7, 10, 14 }, { 0, 5, 7 }, { 0, 4, 7 }, { 0, 4, 7, 11 } };
    return t;
}
std::vector<std::string> resonatorChordNames() { return { "OCTAVES", "FIFTHS", "MIN", "MIN7", "MIN9", "SUS4", "MAJ", "MAJ7" }; }
const char* const kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

class ResonatorModule : public Module
{
public:
    enum { MIX, DECAY, DAMP, SHIFT, SPREAD, DRIVE, TONE, LEVEL, CHORD, OCTAVE, ROOT, FREEZE, KEYFOLLOW };
    static constexpr int kRes = 6;

    std::string id() const override { return "RESONATOR"; }
    double tailSeconds() const override { return 10.0; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("mix", "MIX", 0.5f).prim(),
                 knob ("decay", "DECAY", 0.2f, 10.0f, 2.0f, "s", 2.0f).prim().dir (1),
                 pct ("damp", "DAMP", 0.4f).prim(),
                 knob ("shift", "SHIFT", -12.0f, 12.0f, 0.0f, "st").prim(),
                 pct ("spread", "SPREAD", 0.5f).prim(),
                 pct ("drive", "DRIVE", 0.0f).prim().dir (1),
                 bip ("tone", "TONE").prim(),
                 levelParam().prim(),
                 choice ("chord", "CHORD", resonatorChordNames(), 3),
                 choice ("octave", "OCTAVE", { "C1", "C2", "C3", "C4" }, 2),
                 choice ("root", "ROOT", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0),
                 toggle ("freeze", "FREEZE").performs().noPreset(),
                 toggle ("key_follow", "KEY FOLLOW", true).noPreset().forget() };
    }

    void prepare (double rate, int) override
    {
        sr = rate;
        for (auto& d : lines) d.prepare ((int) (rate / 15.0) + 8);
        tone.prepare (rate);
        dcA = std::exp (-kTwoPi * 12.0f / (float) rate);
        glide = std::exp (-1.0f / (float) (0.03 * rate));
        reset();
    }
    void reset() override
    {
        for (auto& d : lines) d.clear();
        for (int k = 0; k < kRes; ++k) { lp[k] = 0.0f; period[k] = -1.0f; }
        tone.reset();
        dcX = dcY = 0.0f; outX[0] = outX[1] = outY[0] = outY[1] = 0.0f;
        inGain = 1.0f;
    }

    // The resonators' notes now (MIDI note numbers, fractional with SHIFT gliding), from the knobs and the key.
    void notes (const Ctx& c, float* out, int& count, int& rootPc) const
    {
        rootPc = c.b (KEYFOLLOW) && c.keyRoot >= 0 ? c.keyRoot : c.ci (ROOT);
        const auto& ch = resonatorChords()[(size_t) juce::jlimit (0, (int) resonatorChords().size() - 1, c.ci (CHORD))];
        count = std::min (kRes, 3 * (int) ch.size());
        const float root = (float) (12 * (c.ci (OCTAVE) + 2) + rootPc) + std::round (c.v (SHIFT));
        for (int k = 0; k < count; ++k) out[k] = root + (float) (ch[(size_t) k % ch.size()] + 12 * (k / (int) ch.size()));
    }

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        float note[kRes]; int count = 0, rootPc = 0;
        notes (c, note, count, rootPc);
        shownRoot = rootPc; shownCount = count; shownChord = c.ci (CHORD); shownKey = c.b (KEYFOLLOW) && c.keyRoot >= 0;
        for (int k = 0; k < count; ++k) shownNote[k] = (int) std::lround (note[k]);
        // Per resonator: the loop low-pass, the delay (one period less the low-pass's phase delay
        // at the note, less the one sample the read is behind), the feedback for DECAY.
        const float damp = c.v (DAMP);
        const float cutHz = 18000.0f * std::pow (600.0f / 18000.0f, damp);
        const bool freeze = c.b (FREEZE);
        // FREEZE as far as it has got (the input's fade): the low-pass opens, DECAY goes to 10 minutes.
        const float fz = 1.0f - inGain;
        const float a = 1.0f - (1.0f - (1.0f - std::exp (-kTwoPi * std::min (cutHz, 0.45f * (float) sr) / (float) sr))) * (1.0f - fz);
        const float t60 = c.v (DECAY) + (600.0f - c.v (DECAY)) * fz;
        float target[kRes], g[kRes], inK[kRes];
        for (int k = 0; k < kRes; ++k)
        {
            if (k >= count) { target[k] = -1.0f; g[k] = 0.0f; inK[k] = 0.0f; continue; }
            const float hz = 440.0f * std::pow (2.0f, (note[k] - 69.0f) / 12.0f);
            const float w = kTwoPi * hz / (float) sr;
            const float ph = std::atan2 ((1.0f - a) * std::sin (w), 1.0f - (1.0f - a) * std::cos (w));   // the low-pass's phase lag
            const float p = (float) sr / hz;
            target[k] = std::max (2.0f, p - 1.0f - ph / w);
            g[k] = std::pow (10.0f, -3.0f * p / (t60 * (float) sr));
            const float b = 1.0f - a, ga = g[k] * a, cc = 1.0f + b * b - ga * ga;
            const float meanGain = 1.0f + ga * ga / std::sqrt (std::max (1.0e-12f, cc * cc - 4.0f * b * b));
            inK[k] = 1.0f / std::sqrt (meanGain * (float) count);
        }
        const float drv = 0.25f + 3.0f * c.v (DRIVE), drvInv = 1.0f / drv;
        // The input: TONE's filter, DC out.
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        jassert (c.n <= kScratch);
        for (int i = 0; i < c.n; ++i) { scratchL[i] = L[i]; scratchR[i] = R[i]; }
        float* ins[2] = { scratchL, scratchR };
        tone.process (ins, 2, c.n, c.v0 (TONE), c.v (TONE), 0.15f);
        const float spread = c.v (SPREAD);
        float panL[kRes], panR[kRes];
        for (int k = 0; k < kRes; ++k)
        {
            const float side = (k & 1) ? 1.0f : -1.0f, pos = 0.5f + 0.5f * side * spread * (1.0f - 0.25f * (float) (k / 2));
            // Equal power, scaled so a resonator in the middle is at unity in each channel.
            panL[k] = 1.41421356f * std::cos (0.5f * kPi * pos); panR[k] = 1.41421356f * std::sin (0.5f * kPi * pos);
        }
        const float m0 = c.v0 (MIX), m1 = c.v (MIX);
        const float freezeStep = 1.0f / (float) (0.03 * sr);
        float peak = 0.0f;
        for (int i = 0; i < c.n; ++i)
        {
            // FREEZE: the input out (and back) over 30 ms.
            inGain = freeze ? std::max (0.0f, inGain - freezeStep) : std::min (1.0f, inGain + freezeStep);
            float x = 0.5f * (scratchL[i] + scratchR[i]) * inGain;
            dcY = x - dcX + dcA * dcY; dcX = x; x = dcY;
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < count; ++k)
            {
                if (period[k] < 0.0f) period[k] = target[k];
                period[k] = target[k] + glide * (period[k] - target[k]);
                // The output is the loop's delayed signal: what goes in comes out a period later, a
                // different time for each resonator, so the input never adds up straight across them.
                const float back = lines[k].read (period[k]);
                lp[k] += a * (back - lp[k]);
                lines[k].push (fastTanh (drv * (x * inK[k] + g[k] * lp[k])) * drvInv);
                wl += back * panL[k]; wr += back * panR[k];
            }
            // The wet's DC out (the loops pass it), then MIX.
            for (int ch = 0; ch < 2; ++ch)
            {
                const float v = ch == 0 ? wl : wr;
                outY[ch] = v - outX[ch] + dcA * outY[ch]; outX[ch] = v;
            }
            const float mix = m0 + (m1 - m0) * (float) (i + 1) / (float) c.n;
            L[i] = L[i] * (1.0f - mix) + outY[0] * mix;
            R[i] = R[i] * (1.0f - mix) + outY[1] * mix;
            peak = std::max (peak, std::max (std::fabs (outY[0]), std::fabs (outY[1])));
        }
        // Resonators no longer in the chord fall silent.
        for (int k = count; k < kRes; ++k) if (period[k] >= 0.0f) { lines[k].clear(); lp[k] = 0.0f; period[k] = -1.0f; }
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
        wetPk = std::max (peak, wetPk.load() * 0.9f);
    }

    int displayHeight() const override { return 64; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "RESONATING");
        const int root = shownRoot.load(), n = shownCount.load();
        juce::String notesText;
        for (int k = 0; k < n; ++k) notesText << (k ? " " : "") << kNoteNames[((shownNote[k].load() % 12) + 12) % 12] << juce::String (shownNote[k].load() / 12 - 1);
        const auto names = resonatorChordNames();
        ui::label (g, juce::String (kNoteNames[root]) + " " + juce::String (names[(size_t) juce::jlimit (0, (int) names.size() - 1, shownChord.load())])
                      + (shownKey.load() ? "  (KEY)" : "  (ROOT)"), r.reduced (10, 16).removeFromTop (14), ui::kText, 11.0f);
        ui::label (g, notesText, r.reduced (10, 16).withTrimmedTop (15).removeFromTop (12), ui::kFaint, 10.0f);
        ui::levelBar (g, r.reduced (10, 4).removeFromBottom (8), wetPk.load());
    }

private:
    double sr = 48000.0;
    DelayLine lines[kRes];
    float lp[kRes] {}, period[kRes] {};
    DJFilter tone;
    float dcA = 0.999f, dcX = 0.0f, dcY = 0.0f, outX[2] {}, outY[2] {}, glide = 0.999f, inGain = 1.0f;
    static constexpr int kScratch = 1024;            // a chunk is at most 64 samples (Module.h)
    float scratchL[kScratch] {}, scratchR[kScratch] {};
    std::atomic<int> shownRoot { 0 }, shownCount { 0 }, shownChord { 0 }, shownNote[kRes] {};
    std::atomic<bool> shownKey { false };
    std::atomic<float> wetPk { 0.0f };
};
ModulePtr makeRESONATOR() { return std::make_unique<ResonatorModule>(); }
} // namespace sf
