// DECK: two-deck DJ player inside the live set. Tracklist, beat-grid analysis, LUFS-normalised
// trim, tempo sync to Live with key lock (Signalsmith Stretch, MIT), phrase-quantised starts,
// hot cues from MIDI, loop roll, per-deck isolator + filter, crossfader, CUE output, track log.
#include "Kit.h"
#include "../core/BandLimited.h"
#include "../core/Loudness.h"
#include "../core/Resample.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace sf
{
struct TrackData
{
    juce::AudioBuffer<float> buf;
    double sr = 48000.0;
    double bpm = 120.0;
    double firstBeat = 0.0;     // samples
    float lufs = -14.0f;
    juce::String name, path;
};
using TrackPtr = std::shared_ptr<const TrackData>;

// ---- analysis ------------------------------------------------------------------------------
static void analyseTrack (TrackData& t)
{
    const int n = t.buf.getNumSamples();
    const double sr = t.sr;
    // Onset envelope: positive flux of low+mid energy in 512-sample hops.
    const int hop = 512;
    std::vector<float> env;
    Svf lp; lp.set (2000.0f, 0.7f, sr);
    float prev = 0.0f;
    for (int i = 0; i + hop <= n; i += hop)
    {
        float e = 0.0f;
        for (int k = 0; k < hop; ++k) { const float x = lp.tickLP (0.5f * (t.buf.getSample (0, i + k) + t.buf.getSample (1, i + k))); e += x * x; }
        e = std::sqrt (e / hop);
        env.push_back (std::max (0.0f, e - prev));
        prev = e;
    }
    // Autocorrelation over 80..180 BPM, weighted toward 110..145.
    const double hopsPerSec = sr / hop;
    double bestScore = -1.0, bestBpm = 120.0;
    const size_t m = std::min<size_t> (env.size(), (size_t) (hopsPerSec * 120.0));
    for (double bpm = 80.0; bpm <= 180.0; bpm += 0.05)
    {
        const double lag = hopsPerSec * 60.0 / bpm;
        double s = 0.0;
        for (size_t i = 0; i + (size_t) (lag * 4) + 2 < m; i += 1)
        {
            for (int k = 1; k <= 4; ++k)
            {
                const double p = i + lag * k;
                const size_t pi = (size_t) p;
                const double fr = p - pi;
                s += env[i] * (env[pi] * (1.0 - fr) + env[pi + 1] * fr);
            }
        }
        const double w = 1.0 - 0.25 * std::min (1.0, std::abs (bpm - 128.0) / 40.0);
        if (s * w > bestScore) { bestScore = s * w; bestBpm = bpm; }
    }
    t.bpm = std::round (bestBpm * 10.0) / 10.0;
    // Phase: offset maximising onset energy on the beat grid (first 60 s).
    const double lag = hopsPerSec * 60.0 / t.bpm;
    double bestPh = 0.0, bestE = -1.0;
    for (double ph = 0.0; ph < lag; ph += 0.25)
    {
        double s = 0.0;
        for (double p = ph; p < (double) std::min<size_t> (env.size(), (size_t) (hopsPerSec * 60.0)) - 1; p += lag) s += env[(size_t) p];
        if (s > bestE) { bestE = s; bestPh = ph; }
    }
    t.firstBeat = bestPh * hop;
    // Refine tempo and phase to sample accuracy: detect onsets on 64-sample frames, assign each
    // to its nearest beat, and fit onset time = firstBeat + k * samplesPerBeat (least squares).
    {
        const int w = 64;
        Svf lp2; lp2.set (2000.0f, 0.7f, sr);
        std::vector<float> fe;
        for (int i = 0; i + w <= n; i += w)
        {
            float e = 0.0f;
            for (int k = 0; k < w; ++k) { const float x = lp2.tickLP (t.buf.getSample (0, i + k) + t.buf.getSample (1, i + k)); e += x * x; }
            fe.push_back (e);
        }
        double mean = 0; for (float v : fe) mean += v; mean /= std::max<size_t> (1, fe.size());
        std::vector<double> on;
        int last = -100000;
        for (size_t i = 8; i < fe.size(); ++i)
        {
            float prevAvg = 0; for (size_t k = i - 8; k < i; ++k) prevAvg += fe[k]; prevAvg /= 8.0f;
            if (fe[i] > prevAvg * 4.0f && fe[i] > mean * 0.5 && (int) i - last > (int) (0.08 * sr / w)) { on.push_back ((double) i * w); last = (int) i; }
        }
        double spb = sr * 60.0 / t.bpm, first = t.firstBeat;
        for (int iter = 0; iter < 3 && on.size() >= 8; ++iter)
        {
            double sk = 0, st = 0, skk = 0, skt = 0; int cnt = 0;
            for (double o : on)
            {
                const double k = std::round ((o - first) / spb);
                if (std::abs (o - (first + k * spb)) > spb * 0.15) continue;
                sk += k; st += o; skk += k * k; skt += k * o; ++cnt;
            }
            if (cnt < 8) break;
            const double den = cnt * skk - sk * sk;
            if (std::abs (den) < 1e-9) break;
            spb = (cnt * skt - sk * st) / den;
            first = (st - spb * sk) / cnt;
        }
        if (spb > sr * 60.0 / 200.0 && spb < sr * 60.0 / 60.0)
        {
            t.bpm = sr * 60.0 / spb;
            while (first < 0.0) first += spb;
            while (first - spb >= 0.0) first -= spb;
            t.firstBeat = first;
        }
    }
    // Integrated loudness (BS.1770 K-weighting, 400 ms blocks, -70 / -10 gates).
    Biquad sh[2], hp[2];
    for (int c = 0; c < 2; ++c) LoudnessMeter::kWeighting (sr, sh[c], hp[c]);
    const int blk = (int) (0.4 * sr);
    std::vector<double> blocks;
    for (int i = 0; i + blk <= n; i += blk / 4)
    {
        double s = 0.0;
        for (int k = 0; k < blk; ++k)
            for (int c = 0; c < 2; ++c) { const float y = hp[c].tick (sh[c].tick (t.buf.getSample (c, i + k))); s += y * y; }
        blocks.push_back (s / blk);
    }
    double sum = 0; size_t cnt = 0;
    for (double b : blocks) if (-0.691 + 10 * std::log10 (b + 1e-12) > -70.0) { sum += b; ++cnt; }
    if (cnt > 0)
    {
        const double rel = -0.691 + 10 * std::log10 (sum / cnt + 1e-12) - 10.0;
        double s2 = 0; size_t c2 = 0;
        for (double b : blocks) if (-0.691 + 10 * std::log10 (b + 1e-12) > rel) { s2 += b; ++c2; }
        t.lufs = c2 > 0 ? (float) (-0.691 + 10 * std::log10 (s2 / c2 + 1e-12)) : -30.0f;
    }
}

static TrackPtr loadTrack (const juce::String& path, double sr, const std::atomic<bool>& cancel, double cachedBpm = 0.0, double cachedFirst = 0.0, float cachedLufs = 0.0f)
{
    if (cancel.load()) return nullptr;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (path)));
    if (r == nullptr) return nullptr;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60 * 15));
    juce::AudioBuffer<float> in (2, len);
    r->read (&in, 0, len, 0, true, true);
    if (r->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, len);
    auto t = std::make_shared<TrackData>();
    t->sr = sr; t->path = path; t->name = juce::File (path).getFileNameWithoutExtension();
    const double ratio = r->sampleRate / sr;
    const int outN = (int) (len / ratio);
    t->buf.setSize (2, outN);
    for (int c = 0; c < 2; ++c)
    {
        if (std::abs (ratio - 1.0) < 1e-9) t->buf.copyFrom (c, 0, in, c, 0, len);
        else SincResampler::process (in.getReadPointer (c), len, t->buf.getWritePointer (c), outN, r->sampleRate, sr);
    }
    if (cancel.load()) return nullptr;
    if (cachedBpm > 0.0) { t->bpm = cachedBpm; t->firstBeat = cachedFirst * sr; t->lufs = cachedLufs; }   // already analysed (grid in seconds)
    else analyseTrack (*t);
    return t;
}

// ---- module --------------------------------------------------------------------------------
class DeckModule : public Module, private juce::Timer
{
public:
    enum { XFADE, FILTA, FILTB, LOWA, LOWB, ROLL, GATE, LEVEL, PLAYA, PLAYB, MIDA, HIGHA, MIDB, HIGHB, QUANT, KEYLOCK, CUEA, CUEB, TARGET, RES };
    DeckModule() : pool (1), loadPool (1) { startTimerHz (10); }
    ~DeckModule() override
    {
        *aliveFlag = false;
        cancel = true;              // loaders stop at their next check
        stopTimer();
        loadPool.removeAllJobs (true, 3000);
        pool.removeAllJobs (true, 3000);
    }
    std::string id() const override { return "DECK"; }
    bool instrument() const override { return true; }
    bool takesDropCut (const Ctx&) const override { return false; }   // whole tracks: not SF BUILD's cut
    std::vector<std::string> extraOutputs() const override { return { "CUE" }; }
    std::vector<ParamSpec> params() const override
    {
        auto play = [] (const char* id, const char* nm) { auto p = toggle (id, nm).performs(); p.song = false; return p; };
        return { knob ("xfade", "XFADE", 0.0f, 1.0f, 0.0f, "%").prim().noGlide(),
                 bip ("filter_a", "FILTER A").prim(), bip ("filter_b", "FILTER B").prim(),
                 pct ("low_a", "LOW A", 0.75f).prim(), pct ("low_b", "LOW B", 0.75f).prim(),
                 choice ("roll", "ROLL", { "OFF", "1/4", "1/8", "1/16", "1/32" }, 0).prim(),
                 pct ("gate", "GATE", 0.0f).prim(),
                 levelParam (-3.0f).prim(),
                 play ("play_a", "PLAY A"), play ("play_b", "PLAY B"),
                 pct ("mid_a", "MID A", 0.75f), pct ("high_a", "HIGH A", 0.75f),
                 pct ("mid_b", "MID B", 0.75f), pct ("high_b", "HIGH B", 0.75f),
                 choice ("quant", "QUANT", { "1 BEAT", "1 BAR", "4 BAR", "8 BAR", "16 BAR" }, 2).forget(),
                 toggle ("key_lock", "KEY LOCK", true).forget(),
                 toggle ("cue_a", "CUE A").performs(), toggle ("cue_b", "CUE B").performs(),
                 knob ("target", "TRIM TARGET", -16.0f, -6.0f, -10.0f, "LUFS").forget(),
                 pct ("res", "FILTER RES", 0.2f) };
    }
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP TRACKS INTO THE LIST"; }
    void loadFiles (const juce::StringArray& files) override
    {
        for (auto& f : files) if (isAudioFile (f)) { Entry e; e.path = f; e.name = juce::File (f).getFileNameWithoutExtension(); list.push_back (e); analyseEntry ((int) list.size() - 1); }
    }

    void prepare (double rate, int mb) override
    {
        const bool srChanged = std::abs (rate - sr) > 1.0;
        sr = rate;
        allocTap.setIdentity (ROLE_DECK, "DECK");
        allocTap.prepare (rate);
        for (auto& d : decks)
        {
            d.stretch.presetDefault (2, (float) rate);
            d.synced = false;
            d.filter.prepare (rate);
            for (auto& row : d.xo) for (auto& x : row) x.reset();
            // Room for a full seek at the most extreme tempo ratio (Live 400 BPM over a 50 BPM
            // track) at any sample rate: the seek length grows with both.
            d.scratchIn.setSize (2, std::max (65536, d.stretch.outputSeekLength (8.0f) + 8 * 64 + 1024));
            d.scratchOut.setSize (2, std::max (mb, 4096));
            d.deck.setSize (2, std::max (mb, 4096));
            primeStretch (d);
        }
        if (srChanged) for (int k = 0; k < 2; ++k) if (! deckPath[k].isEmpty()) loadIntoDeck (k, deckPath[k]);
    }
    // PANIC / fault: clear filters and the stretcher, keep playing (re-synced, faded back in).
    void reset() override
    {
        for (auto& d : decks)
        {
            d.stretch.reset();
            d.synced = false;
            d.fade = 0.0f;
            d.filter.reset();
            for (auto& row : d.xo) for (auto& x : row) x.reset();
        }
        allocTap.reset();
        gateZ = 1.0f;
    }

    struct Deck
    {
        const TrackData* track = nullptr;   // owned by `owned` (message side); never freed on the audio thread
        bool playing = false, armed = false, stopArmed = false, synced = false, rolling = false;
        double startPpq = 0.0, cueBeat = 0.0, fedPos = 0.0, rollStart = 0.0, lastBeat = 0.0;
        int jumpCue = -1;
        float fade = 0.0f, rollMix = 0.0f;
        float g[3] { 1, 1, 1 };
        signalsmith::stretch::SignalsmithStretch<float> stretch;
        DJFilter filter;
        Crossover xo[2][3];
        juce::AudioBuffer<float> scratchIn, scratchOut, deck;
        std::atomic<bool> logPending { false };
    };

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>* extra, juce::MidiBuffer& midi) override
    {
        const int n = c.n;
        static const double qB[] = { 1.0, 4.0, 16.0, 32.0, 64.0 };
        const double quant = qB[juce::jlimit (0, 4, c.ci (QUANT))] * (c.beatsPerBar / 4.0);
        // Hot cues: C1..D#1 deck A, C2..D#2 deck B (jump on the next beat).
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOn()) continue;
            const int note = m.getNoteNumber();
            if (note >= 36 && note <= 39) decks[0].jumpCue = note - 36;
            if (note >= 48 && note <= 51) decks[1].jumpCue = note - 48;
        }
        io.clear();
        if (extra != nullptr) extra->clear();
        // Transport jump (loop brace, restart, relocate): playing decks keep running from where
        // they are, re-anchored by whole beats so they stay on Live's beat grid.
        const bool jumped = expectedPpq > -1e8 && std::abs (c.ppq - expectedPpq) > 0.01;
        if (jumped)
            for (auto& d : decks)
                if (d.playing)
                {
                    d.startPpq += std::round (c.ppq - expectedPpq);
                    d.synced = false;
                    d.fade = 0.0f;
                }
        expectedPpq = c.ppq + n * c.beatsPerSample();
        const float target = c.v (TARGET);
        static const double rollB[] = { 0.0, 1.0, 0.5, 0.25, 0.125 };
        const double rollLen = rollB[juce::jlimit (0, 4, c.ci (ROLL))];
        // Starts, jumps and the trance gate on the groove's grid (KickGroove): a deck lands with the
        // ONE's parts, which come after Live's beat.
        const double late = groove.beats (*c.bus, c.bpm);
        for (int k = 0; k < 2; ++k)
        {
            auto& d = decks[(size_t) k];
            if (auto* p = pending[k].exchange (nullptr)) { d.track = p; current[k] = p; d.playing = false; d.armed = false; d.synced = false; d.jumpCue = -1; }
            const bool want = c.b (k == 0 ? PLAYA : PLAYB);
            if (want && ! d.playing && ! d.armed && d.track != nullptr) { d.armed = true; d.stopArmed = false; }
            if (! want) { d.armed = false; if (d.playing) d.stopArmed = true; }
            d.deck.setSize (2, n, false, false, true);
            d.deck.clear();
            if (d.track == nullptr) continue;
            const auto& t = *d.track;
            const double trackSpb = t.sr * 60.0 / t.bpm;
            const double rate = c.bpm / t.bpm;
            // Start / jump on the grid.
            if (d.armed)
            {
                const int at = c.nextGrid (quant, late);
                if (at >= 0) { d.armed = false; d.playing = true; d.startPpq = c.ppqAt (at); d.cueBeat = hotCue (k, 0); d.synced = false; d.fade = 0.0f; d.logPending = true; }
            }
            if (! d.playing) d.jumpCue = -1;      // hot cues only act on a playing deck
            if (d.playing && d.jumpCue >= 0)
            {
                // Lands on the next beat line: the cue point is exactly on the beat; the few ms
                // before it in this block play the audio just ahead of the cue, faded in.
                const int at = c.nextGrid (1.0, late);
                if (at >= 0) { d.startPpq = c.ppqAt (at); d.cueBeat = hotCue (k, d.jumpCue); d.jumpCue = -1; d.synced = false; d.fade = 0.0f; }
            }
            if (! d.playing) continue;
            const double beat = d.cueBeat + (c.ppq - d.startPpq);   // may be slightly negative at a start: reads silence until the grid
            d.lastBeat = beat;
            const double desired = t.firstBeat + beat * trackSpb;
            if (desired >= t.buf.getNumSamples()) { d.playing = false; continue; }
            auto* dl = d.deck.getWritePointer (0); auto* dr = d.deck.getWritePointer (1);
            if (c.b (KEYLOCK))
            {
                const int L = std::min (d.stretch.outputSeekLength ((float) rate), d.scratchIn.getNumSamples());
                if (! d.synced || std::abs (desired + L - d.fedPos) > 0.05 * c.sr)
                {
                    fillInput (t, desired, L, d.scratchIn);
                    float* ins[2] = { d.scratchIn.getWritePointer (0), d.scratchIn.getWritePointer (1) };
                    d.stretch.outputSeek (ins, L);
                    d.fedPos = desired + L;
                    d.synced = true;
                }
                const double err = desired + L - d.fedPos;
                const int need = juce::jlimit (0, d.scratchIn.getNumSamples(), (int) std::lround (n * rate + err * 0.1));
                fillInput (t, d.fedPos, need, d.scratchIn);
                float* ins[2] = { d.scratchIn.getWritePointer (0), d.scratchIn.getWritePointer (1) };
                float* outs[2] = { dl, dr };
                d.stretch.process (ins, need, outs, n);
                d.fedPos += need;
            }
            else
            {
                d.synced = false;
                for (int i = 0; i < n; ++i) { const double p = desired + i * rate; dl[i] = readAt (t, 0, p, rate); dr[i] = readAt (t, 1, p, rate); }
            }
            // Loop roll (slip): plays the rolled segment over the running track.
            const bool rollOn = rollLen > 0.0;
            if (rollOn && ! d.rolling) { d.rolling = true; d.rollStart = std::floor (beat / rollLen) * rollLen; }
            if (! rollOn) d.rolling = false;
            const float rollTarget = rollOn ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                d.rollMix += 0.004f * (rollTarget - d.rollMix);
                if (d.rollMix < 0.0005f && ! rollOn) break;
                const double b = beat + i * c.beatsPerSample();
                const double len = rollOn ? rollLen : 0.125;
                const double inLoop = std::fmod (std::max (0.0, b - d.rollStart), len);
                const double p = t.firstBeat + (d.rollStart + inLoop) * trackSpb;
                const float edge = (float) std::min (1.0, std::min (inLoop, len - inLoop) * c.samplesPerBeat() / (0.002 * c.sr));
                dl[i] = lerp (dl[i], readAt (t, 0, p, rate) * edge, d.rollMix);
                dr[i] = lerp (dr[i], readAt (t, 1, p, rate) * edge, d.rollMix);
            }
            // Fades on start / stop.
            const float trim = dbToGain (juce::jlimit (-12.0f, 12.0f, target - t.lufs));
            for (int i = 0; i < n; ++i)
            {
                const float f = d.stopArmed ? 0.0f : 1.0f;
                d.fade += (f - d.fade) * 0.003f;
                dl[i] *= d.fade * trim; dr[i] *= d.fade * trim;
            }
            if (d.stopArmed && d.fade < 0.001f) { d.playing = false; d.stopArmed = false; }
        }
        // Per-deck isolator + filter, CUE send, crossfader.
        const float x = c.v (XFADE);
        const float gA = std::cos (x * kPi * 0.5f), gB = std::sin (x * kPi * 0.5f);
        auto iso = [] (float v) { return v <= 0.75f ? std::pow (v / 0.75f, 2.0f) : dbToGain ((v - 0.75f) / 0.25f * 6.0f); };
        for (int k = 0; k < 2; ++k)
        {
            auto& d = decks[(size_t) k];
            if (d.track == nullptr) continue;
            const float tg[3] = { iso (c.v (k == 0 ? LOWA : LOWB)), iso (c.v (k == 0 ? MIDA : MIDB)), iso (c.v (k == 0 ? HIGHA : HIGHB)) };
            for (int ch = 0; ch < 2; ++ch) { d.xo[ch][0].set (220.0f, c.sr); d.xo[ch][1].set (2500.0f, c.sr); d.xo[ch][2].set (2500.0f, c.sr); }
            for (int i = 0; i < n; ++i)
            {
                for (int j = 0; j < 3; ++j) d.g[j] += (tg[j] - d.g[j]) * 0.005f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    float lo, rest, mid, hi, a1, a2;
                    d.xo[ch][0].split (d.deck.getSample (ch, i), lo, rest);
                    d.xo[ch][1].split (rest, mid, hi);
                    d.xo[ch][2].split (lo, a1, a2);
                    d.deck.setSample (ch, i, (a1 + a2) * d.g[0] + mid * d.g[1] + hi * d.g[2]);
                }
            }
            const int fi = k == 0 ? FILTA : FILTB;
            d.filter.process (d.deck.getArrayOfWritePointers(), 2, n, c.v0 (fi), c.v (fi), c.v (RES));
            if (extra != nullptr && c.b (k == 0 ? CUEA : CUEB))
                for (int ch = 0; ch < 2; ++ch) extra->addFrom (ch, 0, d.deck, ch, 0, n);
            for (int ch = 0; ch < 2; ++ch) io.addFrom (ch, 0, d.deck, ch, 0, n, k == 0 ? gA : gB);
        }
        // Trance gate (1/16) on the mix.
        const float gate = c.v (GATE);
        if (gate > 0.001f)
            for (int i = 0; i < n; ++i)
            {
                double ph = std::fmod ((c.ppqAt (i) - late) * 4.0, 1.0);
                if (ph < 0.0) ph += 1.0;
                const float tgt = ph < 0.5 ? 1.0f : 1.0f - gate;
                gateZ += (tgt - gateZ) * 0.01f;
                io.getWritePointer (0)[i] *= gateZ; io.getWritePointer (1)[i] *= gateZ;
            }
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
        allocTap.process (io.getArrayOfWritePointers(), 2, n, true, &c);
        for (int k = 0; k < 2; ++k)
        {
            auto& d = decks[(size_t) k];
            disp[k].playing = d.playing; disp[k].armed = d.armed;
            disp[k].beat = d.lastBeat;
            disp[k].bpm = d.track ? d.track->bpm : 0.0;
        }
        dispHostBpm = c.bpm;
    }

    // ---- state / UI ----
    void save (juce::ValueTree& t) const override
    {
        for (auto& e : list)
        {
            juce::ValueTree v ("TRACK");
            v.setProperty ("path", e.path, nullptr);
            v.setProperty ("bpm", e.bpm, nullptr);
            v.setProperty ("firstSec", e.firstBeat, nullptr);
            v.setProperty ("lufs", e.lufs, nullptr);
            juce::String cues; for (double cb : e.cues) cues << cb << ",";
            v.setProperty ("cues", cues, nullptr);
            t.appendChild (v, nullptr);
        }
        t.setProperty ("deckA", deckPath[0], nullptr);
        t.setProperty ("deckB", deckPath[1], nullptr);
    }
    void load (const juce::ValueTree& t) override
    {
        list.clear();
        for (auto v : t)
        {
            if (v.getType().toString() != "TRACK") continue;
            Entry e; e.path = v.getProperty ("path").toString(); e.name = juce::File (e.path).getFileNameWithoutExtension();
            e.bpm = (double) v.getProperty ("bpm", 0.0); e.firstBeat = (double) v.getProperty ("firstSec", 0.0);
            if (! v.hasProperty ("firstSec")) e.bpm = 0.0;   // older sets stored samples: re-analyse
            e.lufs = (float) (double) v.getProperty ("lufs", -14.0);
            auto toks = juce::StringArray::fromTokens (v.getProperty ("cues").toString(), ",", "");
            for (int i = 0; i < 4 && i < toks.size(); ++i) e.cues[(size_t) i] = toks[i].getDoubleValue();
            e.missing = ! juce::File (e.path).existsAsFile();
            list.push_back (e);
        }
        for (int k = 0; k < 2; ++k)
        {
            deckPath[k] = t.getProperty (k == 0 ? "deckA" : "deckB").toString();
            if (deckPath[k].isNotEmpty() && juce::File (deckPath[k]).existsAsFile()) loadIntoDeck (k, deckPath[k]);
        }
    }

    int displayHeight() const override { return 260; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto top = r.removeFromTop (62);
        for (int k = 0; k < 2; ++k)
        {
            auto p = top.removeFromLeft (k == 0 ? top.getWidth() * 0.5f - 3 : top.getWidth()).withTrimmedLeft (k == 1 ? 3.0f : 0.0f);
            ui::frame (g, p, k == 0 ? "DECK A" : "DECK B");
            auto in = p.reduced (7, 16);
            const auto& dd = disp[k];
            const juce::String state = dd.playing.load() ? "PLAY" : dd.armed.load() ? "ARMED" : "STOP";
            ui::label (g, state, p.reduced (6, 2).removeFromTop (14), dd.playing.load() ? ui::kGreen : dd.armed.load() ? ui::kAmber : ui::kDim, 9.5f, juce::Justification::centredRight);
            ui::lines (g, deckName[k].isEmpty() ? "EMPTY" : deckName[k].toUpperCase(), in.removeFromTop (14), ui::kText, 10.5f, 1);
            const double b = dd.beat.load();
            ui::label (g, juce::String (dd.bpm.load(), 1) + " BPM", in.removeFromTop (13), ui::kDim, 9.5f);
            ui::label (g, dd.playing.load() ? "BAR " + juce::String ((int) std::floor (b / 4.0) + 1) + "." + juce::String ((int) std::floor (std::fmod (b, 4.0)) + 1) : juce::String ("--"),
                       in.removeFromTop (13), ui::kDim, 9.5f);
        }
        r.removeFromTop (6);
        ui::frame (g, r, "TRACKS   A B: LOAD   X: REMOVE");
        auto in = r.reduced (6, 18);
        rowsArea = in;
        const float rh = 18.0f;
        const int maxRows = (int) (in.getHeight() / rh);
        for (int i = 0; i < maxRows && i + scroll < (int) list.size(); ++i)
        {
            const auto& e = list[(size_t) (i + scroll)];
            auto row = in.removeFromTop (rh);
            auto zoneA = row.removeFromLeft (22), zoneB = row.removeFromLeft (22), zoneX = row.removeFromRight (16);
            const bool inA = e.path == deckPath[0], inB = e.path == deckPath[1];
            g.setColour (inA ? ui::kText : ui::kLine); g.drawRect (zoneA.reduced (2), 1.0f);
            ui::label (g, "A", zoneA, inA ? ui::kText : ui::kDim, 9.5f, juce::Justification::centred);
            g.setColour (inB ? ui::kText : ui::kLine); g.drawRect (zoneB.reduced (2), 1.0f);
            ui::label (g, "B", zoneB, inB ? ui::kText : ui::kDim, 9.5f, juce::Justification::centred);
            ui::label (g, "X", zoneX, ui::kFaint, 9.5f, juce::Justification::centred);
            // BPM and LUFS, short: the strip is narrow.
            const juce::String info = e.missing ? "MISSING" : e.analysing ? "ANALYSING" : juce::String (e.bpm, 1) + " " + juce::String (e.lufs, 1);
            ui::label (g, info, row.removeFromRight (70), e.missing ? ui::kRed : e.analysing ? ui::kAmber : ui::kDim, 9.0f, juce::Justification::centredRight);
            ui::lines (g, e.name.toUpperCase(), row.withTrimmedLeft (4).reduced (0, 3), ui::kText, 10.0f, 1);
        }
        if (list.empty()) ui::lines (g, "DROP AUDIO FILES HERE, OR LOAD (TOP RIGHT). BPM AND LUFS ARE MEASURED ON LOADING.", in.withHeight (30), ui::kFaint, 9.0f, 2);
        if ((int) list.size() > maxRows) ui::label (g, "MORE: CLICK BOTTOM EDGE", r.reduced (8, 2).removeFromBottom (12), ui::kFaint, 9.0f, juce::Justification::centredRight);
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float>, bool down) override
    {
        if (! down || ! rowsArea.contains (p)) { if (down && p.y > rowsArea.getBottom()) scroll = scroll + 5 < (int) list.size() ? scroll + 5 : 0; return; }
        const int row = (int) ((p.y - rowsArea.getY()) / 18.0f) + scroll;
        if (row < 0 || row >= (int) list.size()) return;
        const float x = p.x - rowsArea.getX();
        if (x < 22) loadIntoDeck (0, list[(size_t) row].path);
        else if (x < 44) loadIntoDeck (1, list[(size_t) row].path);
        else if (p.x > rowsArea.getRight() - 16) list.erase (list.begin() + row);
    }

private:
    struct Entry { juce::String path, name; double bpm = 0, firstBeat = 0 /* seconds */; float lufs = -14; std::array<double, 4> cues { 0, 32, 64, 96 }; bool analysing = false, missing = false; };

    double hotCue (int deckIdx, int k) const
    {
        const auto& c = deckCues[(size_t) deckIdx];
        return c[(size_t) juce::jlimit (0, 3, k)];
    }
    static void fillInput (const TrackData& t, double pos, int count, juce::AudioBuffer<float>& dst)
    {
        count = std::min (count, dst.getNumSamples());
        const int start = (int) std::floor (pos);
        const int n = t.buf.getNumSamples();
        for (int c = 0; c < 2; ++c)
        {
            auto* d = dst.getWritePointer (c);
            for (int i = 0; i < count; ++i) { const int s = start + i; d[i] = (s >= 0 && s < n) ? t.buf.getSample (c, s) : 0.0f; }
        }
    }
    static float readAt (const TrackData& t, int c, double p, double rate)
    {
        const int n = t.buf.getNumSamples();
        if (p < 0.0 || (int) p + 2 >= n) return 0.0f;
        return sampleAt (t.buf.getReadPointer (c), n, p, rate);
    }
    // Run the stretcher once on noise (message thread) so every internal buffer reaches its
    // working size before the audio thread uses it.
    void primeStretch (Deck& d)
    {
        Noise nz;
        const int L = d.stretch.outputSeekLength (1.0f) + 8192;
        juce::AudioBuffer<float> in (2, L), out (2, 4096);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < L; ++i) in.setSample (c, i, nz.next() * 0.5f);
        float* ins[2] = { in.getWritePointer (0), in.getWritePointer (1) };
        float* outs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        d.stretch.outputSeek (ins, d.stretch.outputSeekLength (1.0f));
        for (int k = 0; k < 4; ++k)
        {
            float* in2[2] = { ins[0] + 2048 * k, ins[1] + 2048 * k };
            d.stretch.process (in2, 2048, outs, 2048);
        }
        d.stretch.reset();
    }
    void analyseEntry (int idx)
    {
        list[(size_t) idx].analysing = true;
        const juce::String path = list[(size_t) idx].path;
        const double rate = sr;
        auto alive = aliveFlag;
        pool.addJob ([this, path, rate, alive]
        {
            auto t = loadTrack (path, rate, cancel);
            if (! alive->load()) return;
            juce::MessageManager::callAsync ([this, path, t, alive]
            {
                if (! alive->load()) return;
                for (auto& e : list)
                    if (e.path == path) { e.analysing = false; e.missing = t == nullptr; if (t) { e.bpm = t->bpm; e.firstBeat = t->firstBeat / t->sr; e.lufs = t->lufs; } }
            });
        });
    }
    void loadIntoDeck (int k, const juce::String& path)
    {
        deckPath[k] = path;
        deckName[k] = juce::File (path).getFileNameWithoutExtension();
        double bpm = 0.0, first = 0.0; float lufs = 0.0f;
        for (auto& e : list) if (e.path == path) { deckCues[(size_t) k] = e.cues; if (! e.analysing && e.bpm > 0.0) { bpm = e.bpm; first = e.firstBeat; lufs = e.lufs; } }
        const double rate = sr;
        auto alive = aliveFlag;
        // Own queue, so loading a deck never waits behind list analysis.
        loadPool.addJob ([this, k, path, rate, alive, bpm, first, lufs]
        {
            auto t = loadTrack (path, rate, cancel, bpm, first, lufs);
            if (! alive->load() || t == nullptr) return;
            const juce::ScopedLock sl (ownLock);
            owned.push_back ({ t, 0 });
            pending[k].store (t.get());
        });
    }
    void timerCallback() override
    {
        {
            const juce::ScopedLock sl (ownLock);
            for (auto it = owned.begin(); it != owned.end();)
            {
                const auto* p = it->first.get();
                const bool used = p == current[0].load() || p == current[1].load() || p == pending[0].load() || p == pending[1].load();
                it->second = used ? 0 : it->second + 1;
                if (it->second >= 3) it = owned.erase (it); else ++it;   // freed here, on the message thread
            }
        }
        // Track log: one line per deck start, in Documents/SF Live Suite/tracklog.txt.
        for (int k = 0; k < 2; ++k)
            if (decks[(size_t) k].logPending.exchange (false))
            {
                auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SF Live Suite");
                dir.createDirectory();
                dir.getChildFile ("tracklog.txt").appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  DECK " + (k == 0 ? "A" : "B")
                                                              + "  " + deckName[k] + "  " + juce::String (disp[k].bpm.load(), 1) + " BPM\n");
            }
    }

    double sr = 48000.0;
    juce::ThreadPool pool, loadPool;
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);
    std::atomic<bool> cancel { false };
    juce::CriticalSection ownLock;
    std::vector<std::pair<TrackPtr, int>> owned;
    std::atomic<const TrackData*> pending[2] { nullptr, nullptr }, current[2] { nullptr, nullptr };
    std::array<Deck, 2> decks;
    std::vector<Entry> list;
    juce::String deckPath[2], deckName[2];
    std::array<std::array<double, 4>, 2> deckCues { { { 0, 32, 64, 96 }, { 0, 32, 64, 96 } } };
    juce::Rectangle<float> rowsArea;
    int scroll = 0;
    float gateZ = 1.0f;
    AllocTap allocTap;
    double expectedPpq = -1e9;
    struct Disp { std::atomic<bool> playing { false }, armed { false }; std::atomic<double> beat { 0 }, bpm { 0 }; } disp[2];
    std::atomic<double> dispHostBpm { 120 };
    KickGroove groove;                    // where the groove lands against Live's beat
};
ModulePtr makeDECK() { return std::make_unique<DeckModule>(); }
} // namespace sf
