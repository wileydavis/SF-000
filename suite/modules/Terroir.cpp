// SF TERROIR: a convolution reverb for real places. Eight PLACE slots, each an impulse response you
// captured somewhere (a clap, a balloon, or the sweep file played from a speaker and recorded) or
// any IR file, stored inside the Live set and recalled by songs. The first eight controls are SF
// REVERB's, in the same order, so it swaps in on a return and the E16's addresses still fit.
//   SIZE     the same place bigger (slower, darker) or smaller: the IR resampled, 50-200 %.
//   DECAY    the tail shortened from the captured one (100 %), by an exponential envelope.
//   DARK     the tail darkens with time (a low-pass falling up to 8 octaves a second).
//   LENGTH   the IR cut to a note length (raised-cosine end); REVERSE plays it backwards (a swell
//            that ends on the hit, LENGTH long); START skips the direct sound and the early part.
//   FREEZE   holds the wet sound's spectrum as an endless texture while the tail dies away.
// IR changes are built off the audio thread (in Live's offline render, in place, so a render never
// depends on thread timing) and swap in through a 4096-sample crossfade with the whole input
// history (see Convolver). Imports run on the same worker thread, never the message thread.
#include "Kit.h"
#include "../core/Capture.h"
#include "../core/Convolver.h"
#include "../core/IrTools.h"
#include <array>
#include <deque>
#include <mutex>

namespace sf
{
namespace
{
static_assert (Convolver::kLatency == Pocket::kLatency, "TERROIR's wet path latency is the Pocket's");

// One slot's capture, as stored in the Live set.
struct Place
{
    juce::String name, kind;
    double sr = 48000.0;
    juce::AudioBuffer<float> audio;          // 1 or 2 channels, onset at 0, ir::kEnergy per channel
    float snrDb = 0.0f, rt60 = 0.0f;
    juce::MemoryBlock flac;
    float flacGain = 1.0f;
};
using PlacePtr = std::shared_ptr<const Place>;

void encode (Place& p)
{
    const int chs = p.audio.getNumChannels(), n = p.audio.getNumSamples();
    float pk = 0.0f;
    for (int c = 0; c < chs; ++c) pk = std::max (pk, p.audio.getMagnitude (c, 0, n));
    juce::AudioBuffer<float> scaled (p.audio);
    p.flacGain = pk > 0.999f ? pk / 0.999f : 1.0f;
    scaled.applyGain (1.0f / p.flacGain);
    p.flac.reset();
    juce::FlacAudioFormat fmt;
    auto* mos = new juce::MemoryOutputStream (p.flac, false);
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (mos, p.sr, (unsigned int) chs, 24, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (scaled, 0, n);
    else delete mos;
}

PlacePtr decode (const juce::MemoryBlock& mb, double sr, float gain)
{
    juce::FlacAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (new juce::MemoryInputStream (mb, true), true));
    if (r == nullptr || r->lengthInSamples <= 0) return nullptr;
    auto p = std::make_shared<Place>();
    p->sr = r->sampleRate > 0 ? r->sampleRate : sr;
    p->audio.setSize ((int) std::max (1u, std::min (2u, r->numChannels)), (int) r->lengthInSamples);
    r->read (&p->audio, 0, (int) r->lengthInSamples, 0, true, true);
    p->audio.applyGain (gain);
    p->flac = mb;
    p->flacGain = gain;
    return p;
}

// FREEZE: on a press, the wet signal's spectrum (the mean power of two 8192-sample frames) is held
// and played as an endless random-phase texture (Hann frames, 75 % overlap-add, every bin its own
// phase each frame). In over 150 ms, out over 400 ms. A press while it fades keeps the held sound.
class Freeze
{
public:
    static constexpr int kN = 8192, kHop = 2048, kHist = 16384;
    void prepare (double rate)
    {
        sr = rate;
        if (fft == nullptr) fft = std::make_unique<juce::dsp::FFT> (13);
        for (auto& h : hist) h.assign ((size_t) kHist, 0.0f);
        for (auto& o : ola) o.assign ((size_t) kN, 0.0f);
        for (auto& m : mag) m.assign ((size_t) kN / 2 + 1, 0.0f);
        work.assign ((size_t) 2 * kN, 0.0f);
        win.resize ((size_t) kN);
        for (int i = 0; i < kN; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kN));
        for (int i = 0; i < 4096; ++i) { cosT[(size_t) i] = (float) std::cos (6.283185307179586 * i / 4096.0); sinT[(size_t) i] = (float) std::sin (6.283185307179586 * i / 4096.0); }
        upStep = (float) (1.0 / (0.15 * rate));
        downStep = (float) (1.0 / (0.4 * rate));
        reset();
    }
    void reset()
    {
        for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
        for (auto& o : ola) std::fill (o.begin(), o.end(), 0.0f);
        hw = 0; hop = 0; level = 0.0f; held = false;
    }
    float amount() const { return level; }
    void process (float* l, float* r, int n, bool on)
    {
        for (int i = 0; i < n; ++i)
        {
            hist[0][(size_t) hw] = l[i]; hist[1][(size_t) hw] = r[i];
            hw = (hw + 1) & (kHist - 1);
            if (on && ! held) capture();
            level = on ? std::min (1.0f, level + upStep) : std::max (0.0f, level - downStep);
            if (level > 0.0f)
            {
                if (hop == 0) synth();
                const float s = std::sin (1.5707963f * level), g = s * s;
                l[i] += g * ola[0][(size_t) hop];
                r[i] += g * ola[1][(size_t) hop];
                hop = (hop + 1) % kHop;
            }
            else if (held && ! on)
            {
                held = false; hop = 0;
                for (auto& o : ola) std::fill (o.begin(), o.end(), 0.0f);
            }
        }
    }
private:
    void capture()
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& m = mag[(size_t) c];
            std::fill (m.begin(), m.end(), 0.0f);
            for (int f = 0; f < 2; ++f)
            {
                const int end = hw - f * (kN / 2);
                for (int i = 0; i < kN; ++i) work[(size_t) i] = hist[(size_t) c][(size_t) ((end - kN + i) & (kHist - 1))] * win[(size_t) i];
                fft->performRealOnlyForwardTransform (work.data(), true);
                for (int k = 0; k <= kN / 2; ++k) m[(size_t) k] += 0.5f * (work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)]);
            }
            // Random-phase Hann frames at 75 % overlap carry 9/16 of the captured power: 4/3 in amplitude.
            for (auto& v : m) v = std::sqrt (v) * (4.0f / 3.0f);
        }
        held = true;
        hop = 0;
        for (auto& o : ola) std::fill (o.begin(), o.end(), 0.0f);
        for (int k = 0; k < 3; ++k) synth();          // start at full density
    }
    void synth()
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& o = ola[(size_t) c];
            std::copy (o.begin() + kHop, o.end(), o.begin());
            std::fill (o.end() - kHop, o.end(), 0.0f);
            const auto& m = mag[(size_t) c];
            work[0] = work[1] = 0.0f;
            for (int k = 1; k < kN / 2; ++k)
            {
                rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
                const size_t ph = rnd & 4095u;
                work[(size_t) (2 * k)] = m[(size_t) k] * cosT[ph];
                work[(size_t) (2 * k + 1)] = m[(size_t) k] * sinT[ph];
            }
            work[(size_t) kN] = work[(size_t) kN + 1] = 0.0f;
            fft->performRealOnlyInverseTransform (work.data());
            for (int i = 0; i < kN; ++i) o[(size_t) i] += work[(size_t) i] * win[(size_t) i];
        }
    }
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::array<std::vector<float>, 2> hist, ola, mag;
    std::vector<float> work, win;
    std::array<float, 4096> cosT {}, sinT {};
    uint32_t rnd = 0x2545F491u;
    int hw = 0, hop = 0;
    float level = 0.0f, upStep = 0.001f, downStep = 0.001f;
    bool held = false;
};
} // namespace

class TerroirModule : public Module, private juce::Thread
{
public:
    enum { SIZE, DECAY, DARK, PREDELAY, DUCK, FREEZE, THROW, MIX, PLACE, LENGTH, REVERSE, START, WIDTH, LOCUT, DUCKSHAPE, INPUT, DRYON, IMPORT };
    static constexpr int kSlots = 8;
    static constexpr double kMaxIrSec = 16.0;

    TerroirModule() : juce::Thread ("SF TERROIR")
    {
        auto b = std::make_shared<Place>();
        b->name = "BUILT-IN ROOM"; b->kind = "BUILT-IN"; b->sr = 48000.0;
        b->audio = ir::builtInRoom (48000.0);
        b->rt60 = ir::rt60 (b->audio, 48000.0);
        builtIn = b;
        for (auto& v : slotVer) v.store (0);
        startThread (juce::Thread::Priority::low);
    }
    ~TerroirModule() override
    {
        stopThread (30000);                                  // an import in progress finishes first
        for (auto* f : conv.releaseAll()) delete f;
        delete ready.exchange (nullptr);
        delete handoff.exchange (nullptr);
        delete restoredIr;
        delete holdRetired;
        delete offlinePending;
        while (auto* f = popRetired()) delete f;
    }

    std::string id() const override { return "TERROIR"; }
    std::vector<ParamSpec> params() const override
    {
        return { knob ("size", "SIZE", 0.5f, 2.0f, 1.0f, "%", 1.0f).prim().dir (1),
                 knob ("decay", "DECAY", 0.1f, 1.0f, 1.0f, "%").prim().dir (1),
                 pct ("dark", "DARK", 0.0f).prim(),
                 choice ("predelay", "PREDELAY", { "0", "1/64", "1/32", "1/16", "1/8", "1/4" }, 0).prim(),
                 pct ("duck", "DUCK", 0.3f).prim(),
                 toggle ("freeze", "FREEZE").prim(),
                 momentary ("throw", "THROW").prim(),
                 pct ("mix", "MIX", 0.25f).prim().dir (1),
                 choice ("place", "PLACE", { "1", "2", "3", "4", "5", "6", "7", "8" }, 0).noPreset(),
                 choice ("length", "LENGTH", { "FULL", "4 BAR", "2 BAR", "1 BAR", "1/2", "1/4", "1/8", "1/16" }, 0),
                 toggle ("reverse", "REVERSE"),
                 knob ("start", "START", 0.0f, 250.0f, 0.0f, "ms", 60.0f),
                 knob ("width", "WIDTH", 0.0f, 2.0f, 1.0f, "%", 1.0f),
                 knob ("lo_cut", "LO CUT", 20.0f, 800.0f, 120.0f, "Hz", 160.0f),
                 pct ("duck_shape", "DUCK SHAPE", 0.5f),
                 choice ("input", "INPUT", { "ALWAYS", "THROW ONLY" }, 0),
                 dryParam(),
                 importParam() };
    }
    // What a dropped or loaded file is (ir::Source): AUTO decides and says what it took it for; IR
    // FILE keeps every arrival; CLAP cuts before a second clap; SWEEP is the sweep file recorded.
    // How files come in, not a sound: in no song or preset.
    static ParamSpec importParam()
    {
        auto p = choice ("import", "IMPORT", { "AUTO", "IR FILE", "CLAP", "SWEEP" }, 0);
        p.song = false; p.preset = false; p.setup = true;
        return p;
    }
    ir::Source importSource() const
    {
        static const ir::Source s[] = { ir::Source::Auto, ir::Source::IrFile, ir::Source::Clap, ir::Source::Sweep };
        return s[getParam ? juce::jlimit (0, 3, (int) std::lround (getParam (IMPORT))) : 0];
    }
    int latency() const override { return Pocket::kLatency; }
    double tailSeconds() const override { return kMaxIrSec + 4.0; }

    // ------------------------------------------------------------------------------------- files
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP A PLACE: AN IR, A CLAP, OR THE SWEEP RECORDED THERE"; }
    void loadFiles (const juce::StringArray& filesIn) override
    {
        juce::StringArray files (filesIn);
        if (files.isEmpty()) return;
        files.sort (true);
        const int base = getParam ? juce::jlimit (0, kSlots - 1, (int) std::lround (getParam (PLACE))) : 0;
        const auto source = importSource();
        std::vector<Job> add;
        if (files.size() == 2 && pairPrefix (files[0], files[1]).isNotEmpty())
        {
            // "CULVERT L.wav" + "CULVERT R.wav": one stereo place (left first).
            juce::StringArray lr (files);
            if (sideOf (files[0]) == 'R') lr.move (1, 0);
            add.push_back ({ lr, base, pairPrefix (files[0], files[1]), {}, source });
        }
        else
            for (int k = 0; k < std::min (files.size(), kSlots); ++k)
                add.push_back ({ juce::StringArray (files[k]), (base + k) % kSlots, juce::File (files[k]).getFileNameWithoutExtension(), {}, source });
        {
            const std::lock_guard<std::mutex> g (jobLock);
            for (auto& j : add) jobs.push_back (j);
        }
        setStatus ("IMPORTING " + add.front().name.toUpperCase() + (add.size() > 1 ? " AND " + juce::String ((int) add.size() - 1) + " MORE" : juce::String()), 0);
    }

    // ------------------------------------------------------------------------------------- state
    void save (juce::ValueTree& t) const override
    {
        // The tempo last played (LENGTH's notes): a set opens at it, and its first IR is built at it.
        if (lastBpm.load() > 0.0) { t.setProperty ("bpm", lastBpm.load(), nullptr); t.setProperty ("bar", lastBar.load(), nullptr); }
        std::array<PlacePtr, kSlots> s;
        { const std::lock_guard<std::mutex> g (slotLock); s = slots; }
        for (int i = 0; i < kSlots; ++i)
            if (s[(size_t) i] != nullptr && s[(size_t) i]->flac.getSize() > 0)
            {
                const auto& p = *s[(size_t) i];
                juce::ValueTree c ("PLACE");
                c.setProperty ("slot", i, nullptr);
                c.setProperty ("name", p.name, nullptr);
                c.setProperty ("kind", p.kind, nullptr);
                c.setProperty ("sr", p.sr, nullptr);
                c.setProperty ("snr", p.snrDb, nullptr);
                c.setProperty ("rt60", p.rt60, nullptr);
                c.setProperty ("flac", p.flac, nullptr);
                if (std::abs (p.flacGain - 1.0f) > 1.0e-6f) c.setProperty ("gain", p.flacGain, nullptr);
                t.appendChild (c, nullptr);
            }
    }
    void load (const juce::ValueTree& t) override
    {
        // The state's tempo stands in for the host's only until this plugin has played: the host's
        // tempo and meter are newer than any saved with a state.
        const double bpm = t.getProperty ("bpm", 0.0), bar = t.getProperty ("bar", 0.0);
        if (! tempoPlayed.load() && bpm >= 20.0 && bpm <= 400.0 && bar >= 0.5 && bar <= 32.0) { lastBpm.store (bpm); lastBar.store (bar); }
        std::array<PlacePtr, kSlots> s {};
        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            const auto c = t.getChild (i);
            if (! c.hasType ("PLACE")) continue;
            const int slot = (int) c.getProperty ("slot", -1);
            const auto* mb = c.getProperty ("flac").getBinaryData();
            if (slot < 0 || slot >= kSlots || mb == nullptr) continue;
            auto p = decode (*mb, (double) c.getProperty ("sr", 48000.0), (float) (double) c.getProperty ("gain", 1.0));
            if (p == nullptr) continue;
            auto& m = const_cast<Place&> (*p);
            m.name = c.getProperty ("name").toString();
            m.kind = c.getProperty ("kind").toString();
            m.snrDb = (float) (double) c.getProperty ("snr", 0.0);
            m.rt60 = (float) (double) c.getProperty ("rt60", 0.0);
            s[(size_t) slot] = p;
        }
        for (int i = 0; i < kSlots; ++i) setSlot (i, s[(size_t) i]);
    }
    // The state's knobs and places are in: their IR, built here and now (not by the worker, however
    // busy), waits for the audio thread, which takes it when its knobs ask for it (realtimeStep).
    // Before the first prepare there is no rate yet: prepare builds it.
    void restored() override
    {
        const double rate = preparedRate.load (std::memory_order_acquire);
        if (rate <= 0.0 || ! getParam) return;
        const uint64_t tag = ((uint64_t) liveEpoch.load (std::memory_order_acquire) << 32) | (2u * (syncSerial.fetch_add (1) + 1u) + 1u);
        if (auto f = build (keyNow (rate), tag, true)) delete handoff.exchange (f.release(), std::memory_order_acq_rel);
    }

    // ------------------------------------------------------------------------------------- audio
    void prepare (double rate, int mb) override
    {
        sr = rate;
        conv.prepare ((int) (kMaxIrSec * rate));
        maxTaps.store (conv.maxTaps());
        for (auto* f : conv.releaseAll()) delete f;          // built for another rate
        delete ready.exchange (nullptr);
        delete offlinePending;
        offlinePending = nullptr;
        liveEpoch.store (++epoch, std::memory_order_release);   // a new stream: nothing asked for before it is installed
        for (auto& d : pre) d.prepare ((int) (rate * 2.5));
        for (auto& d : dry) d.prepare (Pocket::kLatency + 4);
        pocket.prepare (rate, mb);
        wet.setSize (2, mb);
        mono.assign ((size_t) std::max (mb, 1), 0.0f);
        freeze.prepare (rate);
        dryFol.set (0.002f, 0.25f, rate);
        dryRamp.prepare (rate);
        // The first IR, here and now, for the knobs as they stand (a state's, or turned while the
        // plugin was off): the one the first block asks for, so the first sound is the right room
        // whatever the worker is doing. A state's IR still waiting (restored) is for another stream.
        delete handoff.exchange (nullptr, std::memory_order_acq_rel);
        delete restoredIr;
        restoredIr = nullptr;
        recutF = nullptr;                                    // (released above)
        cutBuf.assign ((size_t) IrFilter::kB2, 0.0f);
        const Key first = keyNow (rate);
        haveKnown = false;
        if (auto f = build (first, syncTag(), true))
        {
            shownTag.store (f->tag, std::memory_order_release);
            conv.offer (f.release());
            have = first; haveKnown = true;
        }
        haveSeq = reqSeq.load (std::memory_order_relaxed);
        postedValid = false;
        offlineKey = first; offlineKeyValid = haveKnown;
        offlineAt = -1000000;                                // the stream starts again at 0
        preparedRate.store (rate, std::memory_order_release);
        reset();
    }
    void reset() override
    {
        conv.reset();
        freeze.reset();
        for (auto& d : pre) d.clear();
        for (auto& d : dry) d.clear();
        lo.reset();
        pocket.reset();
        dispFreeze = 0.0f;
        heard = false;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = c.n;
        wet.setSize (2, n, false, false, true);
        // Which IR the knobs ask for; built off this thread (or here, rendering offline).
        const Key k = keyFrom (c);
        lastBpm.store (c.bpm, std::memory_order_relaxed);
        lastBar.store (c.beatsPerBar, std::memory_order_relaxed);
        tempoPlayed.store (true, std::memory_order_relaxed);
        lastPlaying.store (c.playing || c.offline);          // CAPTURE needs the transport stopped
        // An IR cut again for the host's tempo (recut): the rest of its tail, ahead of the engine
        // (all of it when rendering, where the time is the render's).
        recutTail (c.offline ? 1 << 20 : 1 + n / 1024);
        if (c.offline != wasOffline) modeChange (c.offline);
        if (c.offline) offlineStep (c, k);
        else realtimeStep (k);

        const bool frozen = c.b (FREEZE), throwOn = c.b (THROW), throwOnly = c.ci (INPUT) == 1;
        const float inTarget = frozen ? 0.0f : (throwOnly ? (throwOn ? 1.0f : 0.0f) : (throwOn ? 1.8f : 1.0f));
        static const double preB[] = { 0.0, 1.0 / 16, 0.125, 0.25, 0.5, 1.0 };
        const float preS = (float) (preB[juce::jlimit (0, 5, c.ci (PREDELAY))] * c.samplesPerBeat());
        lo.setHz (c.v (LOCUT), sr);
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        auto* wl = wet.getWritePointer (0); auto* wr = wet.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            inGain += 0.002f * (inTarget - inGain);
            const float m = lo.hp (0.5f * (L[i] + R[i])) * inGain;
            pre[0].push (m);
            mono[(size_t) i] = preS > 1.0f ? pre[0].read (preS) : m;
        }
        if (! heard) for (int i = 0; i < n && ! heard; ++i) heard = mono[(size_t) i] != 0.0f;
        conv.process (mono.data(), wl, wr, n);
        freeze.process (wl, wr, n, frozen);
        dispFreeze = freeze.amount();
        const float w0 = c.v0 (WIDTH), w1 = c.v (WIDTH);
        for (int i = 0; i < n; ++i)
        {
            const float width = w0 + (w1 - w0) * (float) i / (float) n;
            const float mid = 0.5f * (wl[i] + wr[i]), side = 0.5f * (wl[i] - wr[i]) * width;
            wl[i] = mid + side; wr[i] = mid - side;
        }
        // The wet path is already Pocket::kLatency late: the pocket shapes it without delaying it.
        const float duck = c.v (DUCK);
        pocket.process (c, wet.getArrayOfWritePointers(), 2, n, duck, c.v (DUCKSHAPE), nullptr, duck > 0.001f, false, false);
        const float mix = c.v (MIX), dryT = c.b (DRYON) ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            dry[0].push (L[i]); dry[1].push (R[i]);
            const float dl = dry[0].tap (Pocket::kLatency), dr = dry[1].tap (Pocket::kLatency);
            const float env = dryFol.tick (0.5f * (dl + dr));
            const float dg = 1.0f - duck * 0.6f * clamp01 (env * 4.0f);
            const float dryG = dryRamp.tick (dryT);
            L[i] = dl * dryG + wl[i] * mix * dg;
            R[i] = dr * dryG + wr[i] * mix * dg;
        }
        if (auto* r = conv.takeRetired()) retire (r);
        if (holdRetired != nullptr && pushRetired (holdRetired)) holdRetired = nullptr;
    }

    // ------------------------------------------------------------------------------------- display
    int displayHeight() const override { return 184; }
    juce::String statusLine() const override { const juce::SpinLock::ScopedLockType sl (dispLock); return disp.status; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "PLACE");
        Disp d;
        {
            // The IR in the engine (not one being built, nor one built and dropped as out of date).
            const juce::SpinLock::ScopedLockType sl (dispLock);
            d = disp;
            const uint64_t t = shownTag.load (std::memory_order_acquire);
            for (const auto& sh : shapes)
                if (t != 0 && sh.tag == t) { shown = sh; break; }
        }
        d.name = shown.name; d.info = shown.info; d.seconds = shown.seconds; d.env = shown.env;
        std::array<bool, kSlots> filled {};
        { const std::lock_guard<std::mutex> lk (slotLock); for (int i = 0; i < kSlots; ++i) filled[(size_t) i] = slots[(size_t) i] != nullptr; }
        const int sel = getParam ? juce::jlimit (0, kSlots - 1, (int) std::lround (getParam (PLACE))) : 0;
        auto in = r.reduced (8, 16);
        for (int i = 0; i < kSlots; ++i)
        {
            const auto b = slotBox (r, i);
            if (i == sel) { g.setColour (ui::kText); g.fillRect (b); }
            else { g.setColour (filled[(size_t) i] ? ui::kDim : ui::kLine); g.drawRect (b, 1.0f); }
            ui::label (g, juce::String (i + 1), b, i == sel ? ui::kBg : (filled[(size_t) i] ? ui::kText : ui::kFaint), 11.0f, juce::Justification::centred);
        }
        const auto sb = sweepBox (r);
        g.setColour (ui::kDim); g.drawRect (sb, 1.0f);
        ui::label (g, "SWEEP FILE", sb, ui::kText, 10.0f, juce::Justification::centred);
        // CAPTURE: off (white), armed (amber: click a slot), running (STOP, red).
        const auto cb = captureBox (r);
        const int ph = capPhase.load();
        const juce::Colour cc = ph == 1 ? ui::kAmber : ph == 2 ? ui::kRed : ui::kDim;
        g.setColour (cc); g.drawRect (cb, 1.0f);
        ui::label (g, ph == 2 ? "STOP" : "CAPTURE", cb, ph == 0 ? ui::kText : cc, 10.0f, juce::Justification::centred);
        in.removeFromTop (sb.getBottom() - in.getY() + 6.0f);
        auto nameRow = in.removeFromTop (16);
        const bool empty = ! filled[(size_t) sel];
        if (dispFreeze.load() > 0.01f) ui::label (g, "FROZEN", nameRow, ui::kGreen, 10.0f, juce::Justification::centredRight);
        ui::lines (g, empty ? "SLOT " + juce::String (sel + 1) + " EMPTY: BUILT-IN ROOM" : d.name, nameRow.withTrimmedRight (dispFreeze.load() > 0.01f ? 50.0f : 0.0f),
                   empty ? ui::kDim : ui::kText, 12.0f, 1, juce::Justification::centredLeft);
        ui::lines (g, d.info, in.removeFromTop (12), ui::kDim, 9.0f, 1, juce::Justification::centredLeft);
        auto statusRow = in.removeFromBottom (24);
        if (d.status.isEmpty())   // nothing loaded yet: how (a drag from Finder loses the window, which Live hides)
            ui::lines (g, "LOAD (TOP RIGHT) A RECORDING OF THE SWEEP, A CLAP OR AN IR INTO SLOT " + juce::String (sel + 1) + ", OR DROP IT HERE; OR CAPTURE THE ROOM", statusRow, ui::kFaint, 9.0f, 2);
        else
            ui::lines (g, d.status, statusRow, d.statusLevel == 2 ? ui::kRed : d.statusLevel == 1 ? ui::kAmber : ui::kFaint, 9.0f, 2);
        // The IR as it plays now (SIZE, DECAY, DARK, LENGTH, REVERSE applied): level in dB, 0 to -60.
        auto plot = in.reduced (0, 4);
        g.setColour (ui::kGrid);
        for (int db = 0; db >= -60; db -= 20) g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * (float) -db / 60.0f), plot.getX(), plot.getRight());
        if (d.env.empty()) return;
        juce::Path p;
        p.startNewSubPath (plot.getX(), plot.getBottom());
        for (size_t k = 0; k < d.env.size(); ++k)
        {
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) (d.env.size() - 1);
            const float y = plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -d.env[k] / 60.0f);
            p.lineTo (x, y);
        }
        p.lineTo (plot.getRight(), plot.getBottom());
        p.closeSubPath();
        g.setColour (ui::kTrack); g.fillPath (p);
        g.setColour (ui::kText); g.strokePath (p, juce::PathStrokeType (1.0f));
        ui::label (g, juce::String (d.seconds, 2) + " S", plot.removeFromTop (12), ui::kDim, 9.0f, juce::Justification::centredRight);
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        if (! down) return;
        for (int i = 0; i < kSlots; ++i)
            if (slotBox (r, i).contains (p))
            {
                if (setParam) setParam (PLACE, (float) i);
                if (capPhase.load() == 1) startCapture (i);
                return;
            }
        if (sweepBox (r).contains (p)) exportSweep();
        if (captureBox (r).contains (p)) captureClicked();
    }

private:
    struct Cut { int len = 0, fade = 0; bool rev = false; };   // LENGTH's cut of an IR (cutFor)
    // What an IR build is made from. Floats are kept in steps (0.1 %, 0.1 ms): a knob moving by
    // less asks for nothing.
    struct Key
    {
        int slot = 0;
        uint32_t ver = 0;
        int size = 1000, decay = 1000, dark = 0, start = 0, len = 0, srHz = 48000;
        bool rev = false;
        bool operator== (const Key& o) const
        {
            return slot == o.slot && ver == o.ver && size == o.size && decay == o.decay && dark == o.dark && start == o.start
                && len == o.len && srHz == o.srHz && rev == o.rev;
        }
        std::array<int64_t, 10> code() const { return { slot, ver, size, decay, dark, start, len, rev ? 1 : 0, srHz, 0 }; }   // IrFilter::from
    };
    struct Disp
    {
        juce::String name { "BUILT-IN ROOM" }, info, status;
        int statusLevel = 0;
        float seconds = 0.0f;
        std::vector<float> env;
    };
    // note: added to its status line. source: what the files are (IMPORT; an in-set take: KnownSweep).
    struct Job { juce::StringArray files; int slot; juce::String name; juce::String note = {}; ir::Source source = ir::Source::Auto; };

    // The IR the knobs ask for. v: a knob's value; LENGTH's notes at bpm and beatsPerBar.
    template <typename V>
    Key makeKey (V v, double rate, double bpm, double beatsPerBar) const
    {
        static const double lenBars[] = { 0.0, 4.0, 2.0, 1.0 };
        static const double lenBeats[] = { 0.0, 0.0, 0.0, 0.0, 2.0, 1.0, 0.5, 0.25 };
        Key k;
        k.slot = juce::jlimit (0, kSlots - 1, (int) std::lround (v (PLACE)));
        k.ver = slotVer[(size_t) k.slot].load (std::memory_order_acquire);
        k.size = (int) std::lround (v (SIZE) * 1000.0f);
        k.decay = (int) std::lround (v (DECAY) * 1000.0f);
        k.dark = (int) std::lround (v (DARK) * 1000.0f);
        k.start = (int) std::lround (v (START) * 10.0f);
        const int li = juce::jlimit (0, 7, (int) std::lround (v (LENGTH)));
        const double beats = li == 0 ? 0.0 : li <= 3 ? lenBars[li] * beatsPerBar : lenBeats[li];
        k.len = (int) std::lround (beats * (rate * 60.0 / bpm));
        k.rev = v (REVERSE) > 0.5f;
        k.srHz = (int) std::lround (rate);
        return k;
    }
    Key keyFrom (const Ctx& c) const { return makeKey ([&c] (int i) { return c.v (i); }, sr, c.bpm, c.beatsPerBar); }
    // Off the audio thread (prepare, restored): what the first block will ask for. Its knobs start
    // where they stand (the processor's first block glides nothing); the tempo and meter, the host's
    // as this plugin last played them, else the state's, else 120 in 4: the host says its own only
    // with the first block, which cuts this IR again if LENGTH's note comes out another length (recut).
    Key keyNow (double rate) const
    {
        if (! getParam) { Key k; k.slot = 0; k.ver = slotVer[0].load (std::memory_order_acquire); k.srHz = (int) std::lround (rate); return k; }
        const double bpm = lastBpm.load(), bar = lastBar.load();
        return makeKey ([this] (int i) { return getParam (i); }, rate, bpm > 0.0 ? bpm : 120.0, bpm > 0.0 ? bar : 4.0);
    }

    // Audio thread, realtime: hand the key to the worker (a sequence lock: odd while writing). The
    // request carries the epoch, and the result the request's number: see realtimeStep.
    void post (const Key& k)
    {
        const uint32_t s = reqSeq.load (std::memory_order_relaxed);
        reqSeq.store (s + 1, std::memory_order_release);
        std::atomic_thread_fence (std::memory_order_release);
        rqSlot.store (k.slot, std::memory_order_relaxed); rqVer.store (k.ver, std::memory_order_relaxed);
        rqSize.store (k.size, std::memory_order_relaxed); rqDecay.store (k.decay, std::memory_order_relaxed);
        rqDark.store (k.dark, std::memory_order_relaxed); rqStart.store (k.start, std::memory_order_relaxed);
        rqLen.store (k.len, std::memory_order_relaxed); rqSr.store (k.srHz, std::memory_order_relaxed);
        rqRev.store (k.rev, std::memory_order_relaxed); rqEpoch.store (epoch, std::memory_order_relaxed);
        reqSeq.store (s + 2, std::memory_order_release);
        posted = k; postedValid = true; postedSeq = s + 2;
    }
    bool readRequest (Key& k, uint32_t& seq, uint32_t& ep) const
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            const uint32_t s1 = reqSeq.load (std::memory_order_acquire);
            if (s1 & 1u) continue;
            k.slot = rqSlot.load (std::memory_order_relaxed); k.ver = rqVer.load (std::memory_order_relaxed);
            k.size = rqSize.load (std::memory_order_relaxed); k.decay = rqDecay.load (std::memory_order_relaxed);
            k.dark = rqDark.load (std::memory_order_relaxed); k.start = rqStart.load (std::memory_order_relaxed);
            k.len = rqLen.load (std::memory_order_relaxed); k.srHz = rqSr.load (std::memory_order_relaxed);
            k.rev = rqRev.load (std::memory_order_relaxed); ep = rqEpoch.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (reqSeq.load (std::memory_order_relaxed) == s1) { seq = s1; return true; }
        }
        return false;
    }

    // The IR's lifecycle. An epoch begins at prepare and at every change between real time and
    // rendering; nothing asked for in an older one is installed. Within an epoch the worker's results
    // install in the order they were asked for, each newer than what the engine has: a knob that keeps
    // moving is followed, and an answer to an older request than the engine's never lands. While
    // nothing has gone into the engine since prepare / reset, a new IR replaces the old at once (no
    // morph from a room nobody heard).
    uint64_t syncTag() { return ((uint64_t) epoch << 32) | (2u * (syncSerial.fetch_add (1) + 1u) + 1u); }   // odd: built here, not by the worker
    void modeChange (bool toOffline)
    {
        wasOffline = toOffline;
        liveEpoch.store (++epoch, std::memory_order_release);
        postedValid = false;
        if (toOffline) { offlineKey = have; offlineKeyValid = haveKnown; offlineAt = -1000000; }
        else if (offlinePending != nullptr) { retire (offlinePending); offlinePending = nullptr; }
    }
    void install (IrFilter* f)          // the caller has checked: ! heard, or the engine free; holdRetired empty
    {
        shownTag.store (f->tag, std::memory_order_release);
        if (heard) { conv.offer (f); return; }
        IrFilter* out[4];
        for (int i = 0, n = conv.replaceNow (f, out); i < n; ++i) retire (out[i]);
    }
    bool canInstall() const { return holdRetired == nullptr && (! heard || ! conv.busy()); }

    // The first sound at the host's tempo (Nov 6 audit, B04). The first IR after prepare or a state
    // load is built before the host has said its tempo: LENGTH's note may be another length at the
    // first block's tempo and meter. Nothing has gone into the engine yet, so the audio thread cuts
    // that IR again for the knobs as they ask now, from its uncut form (kept with it: build): no
    // build here (the room is the same: only the cut differs), no waiting for the worker, however
    // busy. The head now, before the first sample goes in (16 small transforms), and the tail's
    // first two partitions; the rest one partition a block (recutTail), each long before the engine
    // reaches it: a tail partition meets the first input 4608 + 4096 p samples after it at the
    // soonest. f: restored()'s IR (not in the engine yet) or prepare()'s (in it, nothing gone in).
    bool recut (IrFilter& f, const Key& k)
    {
        if (f.uncut[0].empty() || ! sameBase (f.from, k.code()) || (int) cutBuf.size() < IrFilter::kB2) return false;
        const Cut c = cutFor (f.uncutLen, k);
        if (IrFilter::tailParts (c.len) > f.cap2) return false;
        f.taps = c.len;
        f.p1 = IrFilter::headParts (c.len);
        f.p2 = IrFilter::tailParts (c.len);
        f.from = k.code();
        for (int x = 0; x < 2; ++x)
            for (int p = 0; p < f.p1; ++p)
            {
                cutPart (f, x, p * IrFilter::kB1, IrFilter::kB1, c);
                conv.transform (f, x, false, p, cutBuf.data());
            }
        recutF = &f; recutC = c; recutNext = 0;
        recutTail (2);
        return true;
    }
    // count samples of the cut IR from start, zero past its end, into cutBuf.
    void cutPart (const IrFilter& f, int ch, int start, int count, const Cut& c)
    {
        const float* x = f.uncut[ch].data();
        for (int i = 0; i < count; ++i) cutBuf[(size_t) i] = start + i < c.len ? cutAt (x, c, f.uncutGain, start + i) : 0.0f;
    }
    // Audio thread: the next parts of the recut IR's tail, in order.
    void recutTail (int parts)
    {
        if (recutF == nullptr) return;
        auto& f = *recutF;
        for (int done = 0; done < parts && recutNext < f.p2; ++done, ++recutNext)
            for (int x = 0; x < 2; ++x)
            {
                cutPart (f, x, IrFilter::kT1 + recutNext * IrFilter::kB2, IrFilter::kB2, recutC);
                conv.transform (f, x, true, recutNext, cutBuf.data());
            }
        if (recutNext >= f.p2) recutF = nullptr;
    }
    void realtimeStep (const Key& k)
    {
        // A restored state's IR (restored): the newest waits here until the knobs ask for its key (it
        // is that key's IR whenever they do); then it goes in like any other, and answers to requests
        // made so far are older than it. Built for another stream, or the engine has the key: out.
        if (handoff.load (std::memory_order_relaxed) != nullptr)
            if (auto* h = handoff.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (restoredIr != nullptr) retire (restoredIr);
                restoredIr = h;
            }
        // Nothing in the engine yet (since prepare or reset): the first IR, cut for the tempo and
        // meter before the host told them, is cut again here if the knobs at the host's ask for
        // another LENGTH (or its REVERSE) of the same room: restored()'s, which then goes in below,
        // or prepare()'s, in the engine. Then asked of the worker too, as any new IR (the display's
        // shape comes with its answer: the same IR to the bit, so its crossfade changes nothing).
        bool cutAgain = false;
        if (! heard)
        {
            if (restoredIr != nullptr)
            {
                if ((uint32_t) (restoredIr->tag >> 32) == epoch && restoredIr->from != k.code()) cutAgain = recut (*restoredIr, k);
            }
            else if (haveKnown && ! (k == have))
                if (auto* cur = conv.currentForCut(); cur != nullptr && cur->from == have.code() && recut (*cur, k)) { cutAgain = true; have = k; }
        }
        if (restoredIr != nullptr)
        {
            const bool asked = restoredIr->from == k.code();
            if ((uint32_t) (restoredIr->tag >> 32) != epoch || (asked && haveKnown && k == have)) { retire (restoredIr); restoredIr = nullptr; }
            else if (asked && canInstall())
            {
                install (restoredIr);
                restoredIr = nullptr;
                have = k; haveKnown = true;
                posted = k; postedValid = true; postedSeq = haveSeq = reqSeq.load (std::memory_order_relaxed);
            }
        }
        if (cutAgain) { haveKnown = false; postedValid = false; }    // the worker's copy, for the display
        if (! postedValid || ! (k == posted))
        {
            if (haveKnown && k == have)
            {
                // The engine has it (rendered, or turned back): nothing to build, and no answer to a
                // request made so far may replace it.
                posted = k; postedValid = true; postedSeq = haveSeq = reqSeq.load (std::memory_order_relaxed);
            }
            else post (k);
        }
        if (! canInstall()) return;
        if (auto* f = ready.exchange (nullptr, std::memory_order_acq_rel))
        {
            const uint32_t fe = (uint32_t) (f->tag >> 32), fs = (uint32_t) f->tag;
            if (fe != epoch || (int32_t) (fs - haveSeq) <= 0) { retire (f); return; }   // out of date
            install (f);
            haveSeq = fs;
            haveKnown = postedValid && fs == postedSeq;
            if (haveKnown) have = posted;
        }
    }

    // Offline (Live rendering): build here, at most every 2048 samples of the stream, so a render is
    // the same every time.
    void offlineStep (const Ctx& c, const Key& k)
    {
        if (! (offlineKeyValid && k == offlineKey) && c.samplePos - offlineAt >= 2048)
        {
            offlineAt = c.samplePos;
            offlineKey = k; offlineKeyValid = true;
            auto f = build (k, syncTag());
            delete offlinePending;
            offlinePending = f.release();
        }
        if (offlinePending != nullptr && canInstall())
        {
            install (offlinePending);
            offlinePending = nullptr;
            have = offlineKey; haveKnown = true;
            haveSeq = reqSeq.load (std::memory_order_relaxed);
        }
    }

    PlacePtr placeFor (int slot) const
    {
        const std::lock_guard<std::mutex> g (slotLock);
        auto p = slots[(size_t) juce::jlimit (0, kSlots - 1, slot)];
        return p != nullptr ? p : builtIn;
    }
    void setSlot (int i, PlacePtr p)
    {
        { const std::lock_guard<std::mutex> g (slotLock); slots[(size_t) i] = std::move (p); }
        slotVer[(size_t) i].fetch_add (1, std::memory_order_acq_rel);
    }

    // LENGTH's cut of an IR (whole length n) for the key: its length, its raised-cosine end (15 %
    // of it, 5-60 ms; none when nothing is cut but the engine's limit allows the whole), REVERSE.
    Cut cutFor (int n, const Key& k) const
    {
        Cut c;
        c.len = std::min (n, maxTaps.load());
        if (k.len > 0) c.len = std::min (c.len, k.len);
        c.fade = c.len < n ? juce::jlimit ((int) (0.005 * k.srHz), (int) (0.06 * k.srHz), (int) (0.15 * c.len)) : 0;
        c.rev = k.rev;
        return c;
    }
    // Sample i of the cut IR from channel x of the uncut one: the end faded, the level, reversed.
    // The one rule for the worker's builds and the audio thread's recut: the same IR to the bit.
    static float cutAt (const float* x, const Cut& c, float level, int i)
    {
        const int j = c.rev ? c.len - 1 - i : i;
        float v = x[j];
        if (const int e = c.len - 1 - j; e < c.fade) v *= (float) (0.5 - 0.5 * std::cos (3.14159265358979 * e / c.fade));
        return v * level;
    }
    // The base of a key: all but LENGTH's note and REVERSE (what an uncut IR is for).
    static bool sameBase (const std::array<int64_t, 10>& a, const std::array<int64_t, 10>& b)
    {
        for (int i : { 0, 1, 2, 3, 4, 5, 8 }) if (a[(size_t) i] != b[(size_t) i]) return false;
        return true;
    }

    // The IR the key asks for, ready for the engine. Worker thread (or the audio thread offline).
    // keepUncut: with what it was cut from, so the audio thread can cut it again (recut).
    std::unique_ptr<IrFilter> build (const Key& k, uint64_t tag, bool keepUncut = false)
    {
        const auto src = placeFor (k.slot);
        if (src == nullptr || k.srHz <= 0) return nullptr;
        const double srcSr = src->sr, hostSr = k.srHz;
        int n = src->audio.getNumSamples();
        std::vector<float> ch[2];
        ch[0].assign (src->audio.getReadPointer (0), src->audio.getReadPointer (0) + n);
        ch[1].resize ((size_t) n);
        if (src->audio.getNumChannels() > 1) std::copy (src->audio.getReadPointer (1), src->audio.getReadPointer (1) + n, ch[1].begin());
        else ir::decorrelate (ch[0].data(), ch[1].data(), n, srcSr);
        // START: skip the direct sound and the early part (2 ms fade in).
        const int skip = std::min (n - 1, (int) std::lround (k.start * 0.1 * 0.001 * srcSr));
        if (skip > 0)
            for (auto& v : ch)
            {
                v.erase (v.begin(), v.begin() + skip);
                const int f = std::min ((int) v.size(), (int) (0.002 * srcSr));
                for (int i = 0; i < f; ++i) v[(size_t) i] *= (float) i / (float) f;
            }
        n = (int) ch[0].size();
        // DECAY: the reverberation time scaled by DECAY (an exponential envelope on top of the captured one).
        const float d = (float) k.decay * 0.001f, rt = src->rt60 > 0.05f ? src->rt60 : 1.0f;
        if (d < 0.999f)
        {
            const double step = std::exp (-6.9078 * (1.0 / (rt * d) - 1.0 / rt) / srcSr);
            for (auto& v : ch) { double e = 1.0; for (int i = 0; i < n; ++i) { v[(size_t) i] *= (float) e; e *= step; } }
        }
        // DARK: the tail darkens with time.
        const float dark = (float) k.dark * 0.001f;
        if (dark > 0.001f) for (auto& v : ch) ir::darken (v.data(), n, srcSr, 8.0f * dark * dark);
        // SIZE and the host rate in one resampling. Resampling by a ratio scales the response by it;
        // each part gets its own correction: the host rate's is taken out (the room as loud at 96 kHz as
        // at 48); SIZE keeps its rule (a longer IR holds more energy: 1 / sqrt SIZE); and the capture
        // rate's (an IR is scaled to kEnergy at its own rate, which at 96 kHz is a room 3 dB louder
        // than at 48) is taken out too.
        const double size = k.size * 0.001, ratio = hostSr * size / srcSr;
        if (std::abs (ratio - 1.0) > 1e-9)
            for (auto& v : ch) v = ir::stretch (v.data(), (int) v.size(), ratio);
        const float level = (float) ((srcSr / hostSr) * std::sqrt (48000.0 / srcSr) / std::sqrt (std::max (0.1, size)));
        n = (int) ch[0].size();
        // LENGTH: cut to the note; the engine's limit too.
        const Cut cut = cutFor (n, k);
        const int len = cut.len, room = std::min (n, maxTaps.load());
        std::vector<float> out[2];
        for (int x = 0; x < 2; ++x)
        {
            out[x].resize ((size_t) len);
            for (int i = 0; i < len; ++i) out[x][(size_t) i] = cutAt (ch[x].data(), cut, level, i);
        }
        auto f = makeIrFilter (out[0].data(), out[1].data(), len, keepUncut ? room : 0);
        f->tag = tag;
        f->from = k.code();
        if (keepUncut)
        {
            for (int x = 0; x < 2; ++x) { ch[x].resize ((size_t) room); f->uncut[x] = std::move (ch[x]); }
            f->uncutLen = n;
            f->uncutGain = level;
        }
        for (int x = 0; x < 2; ++x) ch[x] = std::move (out[x]);
        // For the display: the level of the IR as it plays, 160 points over its length.
        Disp nd;
        nd.seconds = (float) (len / hostSr);
        nd.env.assign (160, -120.0f);
        float pk = 1.0e-9f;
        for (int i = 0; i < len; ++i) pk = std::max ({ pk, std::fabs (ch[0][(size_t) i]), std::fabs (ch[1][(size_t) i]) });
        for (int b = 0; b < 160; ++b)
        {
            const int a = (int) ((int64_t) len * b / 160), e = std::max (a + 1, (int) ((int64_t) len * (b + 1) / 160));
            float m = 0.0f;
            for (int i = a; i < std::min (e, len); ++i) m = std::max ({ m, std::fabs (ch[0][(size_t) i]), std::fabs (ch[1][(size_t) i]) });
            nd.env[(size_t) b] = 20.0f * std::log10 (m / pk + 1.0e-9f);
        }
        {
            // Kept by tag: the display shows the one the engine has (see paint).
            const juce::SpinLock::ScopedLockType sl (dispLock);
            const bool user = src != builtIn;
            auto& sh = shapes[(size_t) shapeNext];
            shapeNext = (shapeNext + 1) % (int) shapes.size();
            sh.tag = tag;
            sh.name = user ? src->name : juce::String ("BUILT-IN ROOM");
            sh.info = (user ? src->kind + "  " : juce::String()) + juce::String (src->audio.getNumChannels() > 1 ? "STEREO" : "MONO > STEREO")
                    + "  " + juce::String (src->sr / 1000.0, 1) + " KHZ  RT " + juce::String (src->rt60, 2) + " S"
                    + (user && src->snrDb > 0.0f ? "  " + juce::String ((int) std::lround (src->snrDb)) + " DB OVER THE NOISE" : juce::String());
            sh.seconds = nd.seconds;
            sh.env = std::move (nd.env);
        }
        return f;
    }

    // ------------------------------------------------------------------------------------- worker
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (10);
            while (auto* f = popRetired()) delete f;
            watchCapture();
            runImport();
            Key k;
            uint32_t seq = 0, ep = 0;
            if (readRequest (k, seq, ep) && seq != builtSeq && juce::Time::getMillisecondCounterHiRes() - lastBuild >= 40.0)
            {
                builtSeq = seq;
                if (ep != liveEpoch.load (std::memory_order_acquire)) continue;   // asked for before a prepare or a change of mode
                if (const int ms = ir::testBuildDelayMs.load()) juce::Thread::sleep (ms);
                lastBuild = juce::Time::getMillisecondCounterHiRes();
                if (auto f = build (k, ((uint64_t) ep << 32) | seq)) delete ready.exchange (f.release(), std::memory_order_acq_rel);
            }
        }
    }
    void runImport()
    {
        Job job;
        {
            const std::lock_guard<std::mutex> g (jobLock);
            if (jobs.empty()) return;
            job = jobs.front();
            jobs.pop_front();
        }
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::vector<juce::AudioBuffer<float>> takes;                     // one per file
        std::vector<juce::int64> lengths;                                // each file's own length
        double fileSr = 0.0;
        juce::String tracks;                                             // which tracks of a poly file, for the status line
        for (int k = 0; k < job.files.size(); ++k)
        {
            const juce::File file (job.files[k]);
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
            if (r == nullptr || r->lengthInSamples <= 0 || r->numChannels < 1) { setStatus (job.name.toUpperCase() + ": CAN'T READ IT", 2); return; }
            if (fileSr > 0.0 && std::abs (fileSr - r->sampleRate) > 1.0) { setStatus (job.name.toUpperCase() + ": THE TWO FILES' RATES DIFFER", 2); return; }
            fileSr = r->sampleRate;
            const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) ((ir::kMaxRecording + 1.0) * r->sampleRate));
            // The microphones, not a recorder's mix; of a left or right take, its first one.
            const int nch = (int) r->numChannels;
            const auto names = ir::trackNames (file, r->metadataValues.getValue (juce::WavAudioFormat::bwavDescription, {}), nch);
            auto picks = ir::pickTracks (names, nch);
            if (job.files.size() > 1) picks.resize (1);
            if (job.files.size() == 1 && (nch > 2 || picks.front() != 0))
            {
                juce::StringArray used;
                for (int c : picks) used.add (names.size() == nch && names[c].isNotEmpty() ? names[c].toUpperCase() : "TRACK " + juce::String (c + 1));
                tracks = used.joinIntoString (" + ");
            }
            juce::AudioBuffer<float> b ((int) picks.size(), len);
            constexpr int kChunk = 1 << 16;
            juce::AudioBuffer<float> all (nch, std::min (kChunk, len));
            for (int pos = 0; pos < len; pos += kChunk)
            {
                const int n = std::min (kChunk, len - pos);
                r->read (all.getArrayOfWritePointers(), nch, pos, n);
                for (size_t c = 0; c < picks.size(); ++c) b.copyFrom ((int) c, pos, all, picks[c], 0, n);
            }
            takes.push_back (std::move (b));
            lengths.push_back (r->lengthInSamples);
        }
        ir::Capture cap;
        if (takes.size() == 2 && lengths[0] != lengths[1])
        {
            // A left and a right take recorded one after the other (their lengths differ): nothing
            // ties them in time, so each is analysed alone and the two sit side by side.
            ir::Capture side[2];
            for (size_t k = 0; k < 2; ++k)
            {
                side[k] = ir::analyse (takes[k], fileSr, job.source);
                if (! side[k].ok) { setStatus (juce::File (job.files[(int) k]).getFileNameWithoutExtension().toUpperCase() + ": " + side[k].error, 2); return; }
            }
            cap = ir::joinTakes (side[0], side[1]);
        }
        else
        {
            // One recording: a file, or one split in two of the same length (an IR's L and R).
            juce::AudioBuffer<float> rec;
            if (takes.size() == 1) rec = std::move (takes[0]);
            else
            {
                rec.setSize (2, takes[0].getNumSamples());
                rec.copyFrom (0, 0, takes[0], 0, 0, rec.getNumSamples());
                rec.copyFrom (1, 0, takes[1], 0, 0, rec.getNumSamples());
            }
            // The same signal on both channels (a mono take saved as stereo): a mono place, its tail spread.
            if (rec.getNumChannels() == 2 && ir::sameChannels (rec)) rec.setSize (1, rec.getNumSamples(), true);
            cap = ir::analyse (rec, fileSr, job.source);
        }
        const auto name = job.name.toUpperCase().substring (0, 32);
        if (! cap.ok) { setStatus (name + ": " + cap.error, 2); return; }
        auto p = std::make_shared<Place>();
        p->name = name;
        p->kind = cap.kind;
        p->sr = cap.sr;
        p->audio = std::move (cap.ir);
        p->snrDb = cap.snrDb;
        p->rt60 = cap.rt60;
        encode (*p);
        setSlot (job.slot, p);
        juce::String s = "SLOT " + juce::String (job.slot + 1) + ": " + name + "  " + cap.kind + "  "
                       + juce::String (p->audio.getNumSamples() / p->sr, 2) + " S  RT " + juce::String (cap.rt60, 2) + " S  "
                       + juce::String ((int) std::lround (cap.snrDb)) + " DB OVER THE NOISE";
        if (cap.kind.startsWith ("SWEEP") && std::abs (cap.driftPpm) > 0.5) s << "  CLOCK " << juce::String (cap.driftPpm, 0) << " PPM";
        if (tracks.isNotEmpty()) s << "  FROM " << tracks;
        if (cap.note.isNotEmpty()) s << "  " << cap.note;
        if (job.note.isNotEmpty()) { s << "  " << job.note; setStatus (s, 1); return; }
        setStatus (s, cap.snrDb < 30.0f || cap.note.isNotEmpty() ? 1 : 0);
    }
    void setStatus (const juce::String& s, int level)
    {
        const juce::SpinLock::ScopedLockType sl (dispLock);
        disp.status = s;
        disp.statusLevel = level;
    }

    // Retired IRs travel to the worker through a small single-producer ring.
    void retire (IrFilter* f)
    {
        if (f == recutF) recutF = nullptr;                   // out of the engine: no more to cut
        if (! pushRetired (f)) { if (holdRetired == nullptr) holdRetired = f; else delete f; }
    }
    bool pushRetired (IrFilter* f)
    {
        const int w = rw.load (std::memory_order_relaxed), next = (w + 1) % (int) ring.size();
        if (next == rr.load (std::memory_order_acquire)) return false;
        ring[(size_t) w] = f;
        rw.store (next, std::memory_order_release);
        return true;
    }
    IrFilter* popRetired()
    {
        const int r = rr.load (std::memory_order_relaxed);
        if (r == rw.load (std::memory_order_acquire)) return nullptr;
        auto* f = ring[(size_t) r];
        rr.store ((r + 1) % (int) ring.size(), std::memory_order_release);
        return f;
    }

    // ------------------------------------------------------------------------------------- helpers
    // The strip: the 8 places in a row, CAPTURE and SWEEP FILE under them.
    static juce::Rectangle<float> slotBox (juce::Rectangle<float> r, int i)
    {
        const float w = (r.getWidth() - 16.0f - 7.0f * 3.0f) / (float) kSlots;
        return { r.getX() + 8.0f + (float) i * (w + 3.0f), r.getY() + 16.0f, w, 18.0f };
    }
    static juce::Rectangle<float> captureBox (juce::Rectangle<float> r) { return { r.getX() + 8.0f, r.getY() + 38.0f, (r.getWidth() - 16.0f) * 0.5f - 2.0f, 18.0f }; }
    static juce::Rectangle<float> sweepBox (juce::Rectangle<float> r)
    {
        const auto c = captureBox (r);
        return { c.getRight() + 4.0f, c.getY(), c.getWidth(), 18.0f };
    }

    // ------------------------------------------------------------------------------------- capture
    // In the set (Source/core/Capture.h): CAPTURE arms; a slot clicked then asks SF CAPTURE, on the
    // room mics' track, to play the sweep and record; the take comes back here and is analysed into
    // that slot like a dropped file. Message thread (clicks) and the worker (watchCapture).
    static int takeSeconds() { return (int) std::lround (ir::kLeadSec + ir::kRepeats * (ir::kSweepSec + ir::kGapSec) + capture::kTailSec); }
    void captureClicked()
    {
        const int ph = capPhase.load();
        if (ph == 2)
        {
            // STOP names the request: it holds at whatever stage the take has reached. Not claimed
            // yet, or being written: over now (no SF CAPTURE will take it; the take is never Done,
            // and its file goes); being made or playing: SF CAPTURE ends it (the sweep fades).
            auto& d = Bus::get().d();
            const uint32_t seq = capMine.load();
            d.capCancel = seq;
            capture::advance (d.capStatus, seq, capture::bit (capture::Idle) | capture::bit (capture::Writing), capture::Failed, capture::Cancelled);
            setStatus ("STOPPING THE CAPTURE", 0);
            return;
        }
        if (ph == 1) { capPhase = 0; setStatus ("CAPTURE OFF", 0); return; }
        capPhase = 1;
        setStatus ("CAPTURE ARMED: CLICK THE SLOT TO FILL. SF CAPTURE PLAYS THE SWEEP FROM ITS TRACK FOR " + juce::String (takeSeconds()) + " S", 1);
    }
    void startCapture (int slot)
    {
        auto& bus = Bus::get();
        auto& d = bus.d();
        capPhase = 0;
        if (lastPlaying.load()) { setStatus ("STOP LIVE'S TRANSPORT FIRST: THE SWEEP NEEDS THE ROOM QUIET", 2); return; }
        if (! bus.captureAlive()) { setStatus ("NO SF CAPTURE: PUT IT ON THE ROOM MICS' TRACK, MONITOR IN (ROOM IN SF SHOW)", 2); return; }
        const auto st = capture::status (d.capStatus);
        if (capture::busy (st.state))
        {
            if (Bus::nowMs() - d.capOwnerSeenMs.load() < capture::kLeaseMs) { setStatus ("A CAPTURE IS ALREADY RUNNING", 1); return; }
            // Its recorder stopped answering (removed, its track or Live's audio stood still): over.
            capture::advance (d.capStatus, st.seq, capture::kBusyMask, capture::Failed, capture::Lost);
        }
        // A new request: its state first (an earlier take's Done or Failed is never read as this
        // one's), then what it asks for, then its number, which SF CAPTURE watches.
        const int64_t stamp = juce::Time::currentTimeMillis();
        const uint32_t seq = d.capSeq.load() + 1;
        capStampMine = stamp; capSlotMine = slot; capAskedMs = Bus::nowMs(); lastCapKey = -1;
        d.capStatus = capture::pack (seq, capture::Idle, capture::None);
        d.capStamp = stamp;
        d.capSlot = slot;
        capMine = seq;
        d.capSeq.store (seq, std::memory_order_release);
        capPhase = 2;
        setStatus ("CAPTURE FOR SLOT " + juce::String (slot + 1) + " ASKED", 0);
    }
    // Worker thread: the take's progress on the status line; when it's written, its import.
    void watchCapture()
    {
        if (capPhase.load() != 2) return;
        auto& d = Bus::get().d();
        const int slot = capSlotMine.load();
        const uint32_t mine = capMine.load();
        const auto cs = capture::status (d.capStatus);
        if (cs.seq != mine) { capPhase = 0; setStatus ("CAPTURE REPLACED: ANOTHER SF TERROIR ASKED FOR ONE", 1); return; }
        const int st = cs.state;
        if (st == capture::Idle)                              // not claimed yet (SF CAPTURE takes it within 2 s)
        {
            if (Bus::nowMs() - capAskedMs.load() > 3000
                && capture::advance (d.capStatus, mine, capture::bit (capture::Idle), capture::Failed, capture::NoAnswer))
            { capPhase = 0; setStatus ("SF CAPTURE DIDN'T ANSWER: IS ITS TRACK ON, MONITOR IN?", 2); }
            return;
        }
        const bool clip = d.capPeak.load() >= 0.99f;
        if (capture::busy (st))
        {
            // Its recorder stopped answering (removed with no word, its track or Live's audio stood
            // still): the take is over, and a new one can be asked for.
            if (Bus::nowMs() - d.capOwnerSeenMs.load() > capture::kLeaseMs
                && capture::advance (d.capStatus, mine, capture::kBusyMask, capture::Failed, capture::Lost))
            { capPhase = 0; setStatus ("CAPTURE STOPPED: SF CAPTURE STOPPED ANSWERING (REMOVED, OR ITS TRACK'S AUDIO STOOD STILL)", 2); return; }
            const int sec = (int) (d.capProgress.load() * (float) takeSeconds());
            const int key = st == capture::Writing ? 100000 : sec * 2 + (clip ? 1 : 0);
            if (key == lastCapKey) return;
            lastCapKey = key;
            if (st == capture::Writing) setStatus ("WRITING THE TAKE FOR SLOT " + juce::String (slot + 1), 0);
            else setStatus ("CAPTURING SLOT " + juce::String (slot + 1) + ": " + juce::String (sec) + " OF " + juce::String (takeSeconds()) + " S. QUIET PLEASE"
                            + (clip ? ". THE MICS CLIP: SPEAKER OR GAIN DOWN" : ""), clip ? 1 : 0);
            return;
        }
        capPhase = 0;
        if (st == capture::Done)
        {
            const auto f = capture::fileFor (capStampMine.load());
            const juce::String note = clip ? "THE MICS CLIPPED: TAKE IT AGAIN, SPEAKER OR GAIN DOWN" : "";
            // SF CAPTURE's take is the sweep: analysed as one (and if it didn't come back, it fails
            // and the slot stays as it was; it is never taken for a clap).
            { const std::lock_guard<std::mutex> g (jobLock); jobs.push_back ({ juce::StringArray (f.getFullPathName()), slot, f.getFileNameWithoutExtension(), note, ir::Source::KnownSweep }); }
            setStatus ("ANALYSING THE TAKE FOR SLOT " + juce::String (slot + 1), 0);
            return;
        }
        const int e = cs.error;
        setStatus (e == capture::TransportStarted ? "CAPTURE STOPPED: THE TRANSPORT STARTED"
                 : e == capture::CantWrite ? "CAPTURE STOPPED: SF CAPTURE CAN'T WRITE IN " + capture::folder().getFullPathName().toUpperCase()
                 : e == capture::NoMemory ? "CAPTURE STOPPED: NOT ENOUGH MEMORY"
                 : e == capture::Removed ? "CAPTURE STOPPED: SF CAPTURE WAS REMOVED"
                 : e == capture::Lost ? "CAPTURE STOPPED: SF CAPTURE STOPPED ANSWERING"
                 : e == capture::NoAnswer ? "SF CAPTURE DIDN'T ANSWER: IS ITS TRACK ON, MONITOR IN?" : "CAPTURE STOPPED", e == capture::Cancelled ? 0 : 2);
    }
    void exportSweep()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SF Live Suite").getChildFile ("TERROIR");
        dir.createDirectory();
        auto f = dir.getChildFile ("SF TERROIR SWEEP.wav");
        f.deleteFile();
        const auto b = ir::sweepFile (48000.0);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 48000.0, 1, 24, {}, 0));
        if (w == nullptr) { setStatus ("CAN'T WRITE THE SWEEP FILE IN " + dir.getFullPathName().toUpperCase(), 2); return; }
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        w.reset();
        f.revealToUser();
        setStatus ("SWEEP FILE IN MUSIC/SF LIVE SUITE/TERROIR: PLAY IT FROM THE SPEAKER, RECORD ALL OF IT, DROP THE RECORDING HERE", 0);
    }
    // "CULVERT L" / "CULVERT R" (also LEFT / RIGHT, any separator): the shared name, else empty.
    static juce::String stem (const juce::String& path) { return juce::File (path).getFileNameWithoutExtension().trim(); }
    static char sideOf (const juce::String& path)
    {
        const auto s = stem (path).toUpperCase();
        if (s.endsWith ("LEFT") || s.endsWith (" L") || s.endsWith ("_L") || s.endsWith ("-L")) return 'L';
        if (s.endsWith ("RIGHT") || s.endsWith (" R") || s.endsWith ("_R") || s.endsWith ("-R")) return 'R';
        return 0;
    }
    static juce::String pairPrefix (const juce::String& a, const juce::String& b)
    {
        const char sa = sideOf (a), sb = sideOf (b);
        if (sa == 0 || sb == 0 || sa == sb) return {};
        auto cut = [] (const juce::String& p, char side)
        {
            auto s = stem (p);
            s = s.dropLastCharacters (s.toUpperCase().endsWith (side == 'L' ? "LEFT" : "RIGHT") ? (side == 'L' ? 4 : 5) : 1);
            return s.trimCharactersAtEnd (" _-");
        };
        const auto pa = cut (a, sa), pb = cut (b, sb);
        return pa.equalsIgnoreCase (pb) && pa.isNotEmpty() ? pa : juce::String();
    }

    double sr = 48000.0;
    Convolver conv;
    Freeze freeze;
    DryRamp dryRamp;
    DelayLine pre[1], dry[2];
    OnePole lo;
    Pocket pocket;
    Follower dryFol;
    juce::AudioBuffer<float> wet;
    std::vector<float> mono;
    float inGain = 1.0f;
    std::atomic<float> dispFreeze { 0.0f };

    // Slots: message thread and worker (never the audio thread, which reads only the versions).
    mutable std::mutex slotLock;
    std::array<PlacePtr, kSlots> slots {};
    std::array<std::atomic<uint32_t>, kSlots> slotVer;
    PlacePtr builtIn;

    // Requests (audio thread -> worker) and results (worker -> audio thread); the lifecycle (audio
    // thread and prepare): see modeChange.
    std::atomic<double> lastBpm { 0.0 }, lastBar { 4.0 };     // the tempo last played or the state's (keyNow); 0: neither, 120
    std::atomic<bool> tempoPlayed { false };        // lastBpm is the host's (a block has run): a state's never replaces it
    std::atomic<double> preparedRate { 0.0 };       // 0 until prepare
    std::atomic<IrFilter*> handoff { nullptr };     // a restored state's IR, built off the audio thread (restored)
    IrFilter* restoredIr = nullptr;                 // audio thread: the newest, until its key is asked for
    IrFilter* recutF = nullptr;                     // audio thread: an IR cut again (recut), its tail still to finish
    Cut recutC;
    int recutNext = 0;
    std::vector<float> cutBuf;                      // one tail partition's samples (prepare)
    Key posted; bool postedValid = false;           // the newest request to the worker, this epoch
    uint32_t postedSeq = 0;
    Key have; bool haveKnown = false;               // the newest IR handed to the engine (unknown: one on the way)
    uint32_t haveSeq = 0;                           // no answer to a request up to this one replaces it
    Key offlineKey; bool offlineKeyValid = false;   // rendering: the last IR built here, and when
    int64_t offlineAt = -1000000;
    uint32_t epoch = 1;
    std::atomic<uint32_t> syncSerial { 0 };
    bool wasOffline = false, heard = false;         // heard: anything into the engine since prepare / reset
    std::atomic<uint32_t> liveEpoch { 1 }, rqEpoch { 0 };
    std::atomic<uint64_t> shownTag { 0 };           // the IR in the engine (the display draws its shape)
    std::atomic<uint32_t> reqSeq { 0 }, rqVer { 0 };
    std::atomic<int> rqSlot { 0 }, rqSize { 1000 }, rqDecay { 1000 }, rqDark { 0 }, rqStart { 0 }, rqLen { 0 }, rqSr { 48000 };
    std::atomic<bool> rqRev { false };
    std::atomic<IrFilter*> ready { nullptr };
    std::atomic<int> maxTaps { 1 };
    uint32_t builtSeq = 0;
    double lastBuild = 0.0;
    IrFilter* holdRetired = nullptr;
    IrFilter* offlinePending = nullptr;
    std::array<IrFilter*, 16> ring {};
    std::atomic<int> rw { 0 }, rr { 0 };

    std::mutex jobLock;
    std::deque<Job> jobs;
    juce::SpinLock dispLock;
    Disp disp;                                      // the status line
    struct Shape { uint64_t tag = 0; juce::String name { "BUILT-IN ROOM" }, info; float seconds = 0.0f; std::vector<float> env; };
    std::array<Shape, 16> shapes;                   // the last IRs built, by tag
    int shapeNext = 0;
    Shape shown;                                    // message thread: what the display drew last

    // Capture: 0 off, 1 armed (a slot click asks), 2 asked or running (until it's written or stops).
    std::atomic<int> capPhase { 0 }, capSlotMine { 0 };
    std::atomic<uint32_t> capMine { 0 };            // the request this plugin made
    std::atomic<int64_t> capStampMine { 0 }, capAskedMs { 0 };
    std::atomic<bool> lastPlaying { false };
    std::atomic<int> lastCapKey { -1 };             // the status line shown for the take (reset by a new request)
};

ModulePtr makeTERROIR() { return std::make_unique<TerroirModule>(); }
} // namespace sf
