#include "Engine.h"
#include "core/Processor.h"
#include "core/Registry.h"
#include "core/Bus.h"
#include "core/Loudness.h"
#include <map>
#include <mutex>

namespace sf0
{
static const double kGlideBars[] = { 0, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256 };
static const char* const kGlideNames[] = { "OFF", "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS", "64 BARS", "128 BARS", "256 BARS" };
static const char* const kKeyNames[] = { "OFF", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

double glideBarsFor (float v)   { return kGlideBars[juce::jlimit (0, 11, (int) std::lround (v * 11.0f))]; }
double tempoFor (float v)       { return 60.0 + 140.0 * (double) juce::jlimit (0.0f, 1.0f, v); }
float tempoToValue (double bpm) { return (float) juce::jlimit (0.0, 1.0, (bpm - 60.0) / 140.0); }

// Where each lane sits in the two returns to begin with (SEND A the dub delay, SEND B the reverb).
static const float kSendDefaults[kLanes][2] = {
    { 0.0f, 0.0f }, { 0.12f, 0.0f }, { 0.2f, 0.2f }, { 0.0f, 0.35f }, { 0.3f, 0.2f }, { 0.0f, 0.0f },
    { 0.0f, 0.0f }, { 0.35f, 0.15f }, { 0.45f, 0.3f }, { 0.0f, 0.4f }, { 0.3f, 0.3f } };

static const char* const kModelId = "__model";       // SPACE's reverb, among its setup

struct Engine::Loud { sf::LoudnessMeter m; };
Engine::InsertInst::InsertInst() = default;
Engine::InsertInst::~InsertInst() = default;
Engine::Voice::Voice() = default;
Engine::Voice::~Voice() = default;
Engine::Lane::Lane()
{
    for (int k = 0; k < kSlots; ++k) { ins[k] = nullptr; out[k] = nullptr; slotG[k] = 1.0f; }
    for (int k = 0; k < kStages; ++k) { fadeSeq[k] = 0; fadeDone[k] = 0; fadeDoneAt[k] = 0; fadeBeats[k] = 0.0f; fadeAt[k] = 1.0f; fadeG[k] = 1.0f; }
}
Engine::Lane::~Lane() = default;

// ------------------------------------------------------------------------------------- building
// The longest any insert effect holds its sound back at this sample rate (the distortion's
// oversampling, a few samples). Found once for each rate, from one of each made for the purpose.
static int slowestEffect (double rate)
{
    static std::mutex lock;
    static std::map<int, int> known;
    const std::lock_guard<std::mutex> g (lock);
    const int key = (int) std::lround (rate);
    if (const auto it = known.find (key); it != known.end()) return it->second;
    int most = 0;
    for (auto& d : effects())
    {
        SFProcessor p (sf::makeModule (d.module));
        p.setRateAndBufferSizeDetails (rate, 512);
        p.prepareToPlay (rate, 512);
        most = std::max (most, p.getLatencySamples());
    }
    return known[key] = most;
}

std::unique_ptr<SFProcessor> Engine::makeProc (const char* module, const std::vector<Fixed>& fixed)
{
    auto p = std::make_unique<SFProcessor> (sf::makeModule (module));
    for (auto& f : fixed)
    {
        const int i = p->paramIndex (f.param);
        jassert (i >= 0);
        if (i < 0) continue;
        auto* prm = p->param (i);
        prm->setValueNotifyingHost (prm->convertTo0to1 (f.value));
    }
    p->setPlayHead (this);
    return p;
}

Engine::Bind Engine::makeBind (SFProcessor& p, const KnobDef& k, Control* c)
{
    Bind b;
    if (k.param == nullptr) return b;
    const int i = p.paramIndex (k.param);
    jassert (i >= 0);
    if (i < 0) return b;
    const auto& spec = p.allSpecs()[(size_t) i];
    b.raw = p.apvts.getRawParameterValue (spec.id);
    b.param = p.param (i);
    b.lo = k.lo; b.hi = k.hi;
    if (spec.type == sf::PType::Choice) b.n = (int) spec.choices.size();
    else if (spec.type != sf::PType::Float) b.n = 2;
    if (c != nullptr)
    {
        c->label = k.label;
        c->used = true;
        c->choices.clear();
        c->kind = k.kind == 'b' ? Kind::Bipolar : k.kind == 'm' ? Kind::List : k.kind == 't' ? Kind::Toggle : k.kind == 'h' ? Kind::Hold : Kind::Knob;
        if (spec.type == sf::PType::Choice) for (auto& s : spec.choices) c->choices.add (s);
        c->def = readBind (b);
        c->morph = c->kind == Kind::Knob || c->kind == Kind::Bipolar;
        c->glides = c->morph;
    }
    return b;
}

float Engine::readBind (const Bind& b)
{
    if (b.raw == nullptr) return 0.0f;
    if (b.n > 0) return juce::jlimit (0.0f, 1.0f, b.raw->load() / (float) std::max (1, b.n - 1));
    return juce::jlimit (0.0f, 1.0f, (b.param->convertTo0to1 (b.raw->load()) - b.lo) / std::max (1e-6f, b.hi - b.lo));
}

void Engine::applyBind (const Bind& b, float v)
{
    if (b.raw == nullptr) return;
    if (b.n > 0) b.raw->store (std::round (v * (float) (b.n - 1)));
    else         b.raw->store (b.param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, b.lo + v * (b.hi - b.lo))));
}

Engine::Engine()
{
    controls.resize ((size_t) cx::kCount);
    for (auto& c : controls) { c.used = false; c.snapshot = false; }
    auto named = [this] (int i, juce::String id, juce::String label, Kind k, float def, bool snap, bool morph) -> Control&
    {
        auto& c = controls[(size_t) i];
        c.id = id; c.label = label; c.kind = k; c.def = def; c.snapshot = snap; c.morph = morph; c.glides = morph; c.used = true;
        return c;
    };

    const auto& L = lanes();
    for (int l = 0; l < kLanes; ++l)
    {
        const auto& d = L[(size_t) l];
        auto& ln = lane[(size_t) l];
        auto* first = new Voice();
        first->proc = makeProc (d.module, d.fixed);
        ln.voice = first;
        ln.player.pitched = d.pitched;
        ln.shared = defaultPattern (l);
        const juce::String slug (d.slug);
        for (int k = 0; k < kLaneKnobs; ++k)
        {
            const int i = cx::lane (l, k);
            auto& c = controls[(size_t) i];
            first->bind[k] = makeBind (*first->proc, d.knobs[k], &c);     // (a lane's knobs reach its voice; the table's own bind stays empty)
            c.id = slug + "." + d.knobs[k].param;
            c.snapshot = true;
        }
        named (cx::lane (l, cx::kSendA), slug + ".send_a", "SEND A", Kind::Knob, kSendDefaults[l][0], true, true);
        named (cx::lane (l, cx::kSendB), slug + ".send_b", "SEND B", Kind::Knob, kSendDefaults[l][1], true, true);
        named (cx::lane (l, cx::kEqLow), slug + ".eq_low", "LOW", Kind::Bipolar, 0.5f, true, true);
        named (cx::lane (l, cx::kEqMid), slug + ".eq_mid", "MID", Kind::Bipolar, 0.5f, true, true);
        named (cx::lane (l, cx::kEqHigh), slug + ".eq_high", "HIGH", Kind::Bipolar, 0.5f, true, true);
        for (int s = 0; s < kSlots; ++s)
        {
            insType[(size_t) l][(size_t) s] = -1;
            for (int c = 0; c < kCells; ++c)
            {
                auto& ct = controls[(size_t) cx::ins (l, s, c)];
                ct.id = slug + ".i" + juce::String (s + 1) + ".c" + juce::String (c + 1);
                ct.used = false; ct.snapshot = true; ct.morph = false; ct.glides = false;
            }
        }
        named (cx::lane (l, cx::kMute), slug + ".mute", "MUTE", Kind::Toggle, 0.0f, false, false);
        named (cx::lane (l, cx::kSolo), slug + ".solo", "SOLO", Kind::Toggle, 0.0f, false, false);
        named (cx::lane (l, cx::kSeqMute), slug + ".seq_mute", "SEQ MUTE", Kind::Toggle, 0.0f, false, false);
        for (int s = 0; s < kSlots; ++s)
        {
            named (cx::insMute (l, s), slug + ".i" + juce::String (s + 1) + ".mute", "MUTE", Kind::Toggle, 0.0f, false, false);
            named (cx::insSolo (l, s), slug + ".i" + juce::String (s + 1) + ".solo", "SOLO", Kind::Toggle, 0.0f, false, false);
        }
        static const char* const gn[] = { "HITS", "ROTATE", "ACCENTS", "SEED" };
        static const char* const gi[] = { "gen_hits", "gen_rotate", "gen_accents", "gen_seed" };
        static const float gd[] = { 0.31f, 0.5f, 0.2f, 0.0f };
        for (int k = 0; k < 4; ++k)
        {
            auto& c = named (cx::gen (l, k), slug + "." + gi[k], gn[k], k == 1 ? Kind::Bipolar : k == 3 ? Kind::List : Kind::Knob, gd[k], true, false);
            if (k == 3) for (int q = 1; q <= 16; ++q) c.choices.add (juce::String (q));
        }
    }

    retA = makeProc (returnA().module, returnA().fixed);
    for (int m = 0; m < kSpaceModels; ++m) space[(size_t) m] = makeProc (returnB (m).module, returnB (m).fixed);
    for (int k = 0; k < 4; ++k)
    {
        auto& a = controls[(size_t) (cx::kRet + k)];
        bind[(size_t) (cx::kRet + k)] = makeBind (*retA, returnA().cells[k], &a);
        a.id = juce::String ("ret.dub_") + returnA().cells[k].param; a.snapshot = true;
        // B's four knobs once for each reverb it can be: a knob's place means something else on the other
        for (int m = 0; m < kSpaceModels; ++m)
        {
            auto& b = controls[(size_t) cx::space (m, k)];
            bind[(size_t) cx::space (m, k)] = makeBind (*space[(size_t) m], returnB (m).cells[k], &b);
            b.id = juce::String (m == 0 ? "ret.space_" : "ret.conv_") + returnB (m).cells[k].param; b.snapshot = true;
        }
    }
    named (cx::kRetMS, "ret.dub_mute", "MUTE", Kind::Toggle, 0.0f, false, false);
    named (cx::kRetMS + 1, "ret.dub_solo", "SOLO", Kind::Toggle, 0.0f, false, false);
    named (cx::kRetMS + 2, "ret.space_mute", "MUTE", Kind::Toggle, 0.0f, false, false);
    named (cx::kRetMS + 3, "ret.space_solo", "SOLO", Kind::Toggle, 0.0f, false, false);

    int mk = 0;
    for (auto& st : masterChain())
    {
        master.push_back (makeProc (st.module, st.fixed));
        if (st.knob.label != nullptr && mk < 3)
        {
            auto& c = controls[(size_t) (cx::kMaster + mk)];
            bind[(size_t) (cx::kMaster + mk)] = makeBind (*master.back(), st.knob, &c);
            c.id = juce::String ("mst.") + juce::String (st.knob.label).toLowerCase(); c.snapshot = false; c.morph = false; c.glides = false;
            ++mk;
        }
    }

    named (cx::kTempo, "g.tempo", "TEMPO", Kind::Knob, tempoToValue (132.0), false, false);
    auto& key = named (cx::kKey, "g.key", "KEY", Kind::List, 0.0f, true, false);
    for (auto* n : kKeyNames) key.choices.add (n);
    auto& gl = named (cx::kGlide, "g.glide", "GLIDE", Kind::List, 0.0f, false, false);
    for (auto* n : kGlideNames) gl.choices.add (n);
    for (int k = 0; k < 16; ++k)
        named (cx::kSnap + k, "snap.k" + juce::String (k + 1), {}, Kind::Knob, 0.0f, false, false);

    for (int i = 0; i < cx::kCount; ++i)
    {
        const float d = controls[(size_t) i].def;
        targetV[(size_t) i] = d; shownV[(size_t) i] = d; glideV[(size_t) i] = 0.0f; seqV[(size_t) i] = 0;
        cur[(size_t) i] = d; applied[(size_t) i] = d;
    }
    loud = std::make_unique<Loud>();
    for (auto& p : laneDb) p = -100.0f;
    for (int l = 0; l < kLanes; ++l) initSetup[(size_t) l] = getSetup ({ 0, l, 0 });

    // To begin with the lead has a delay and a radio on it, as the design shows.
    setInsertType (7, 0, 0);
    setInsertType (7, 1, 5);
    set (cx::ins (7, 1, 7), 0.35f);
}

Engine::~Engine()
{
    collectRetired (true);
    for (auto* p : parked) delete p;
    for (auto& ln : lane)
    {
        for (auto& i : ln.ins) delete i.exchange (nullptr);
        for (auto& i : ln.out) delete i.exchange (nullptr);
        delete ln.voice.exchange (nullptr);
        delete ln.voiceOut.exchange (nullptr);
    }
}

int Engine::find (const juce::String& id) const
{
    for (int i = 0; i < cx::kCount; ++i) if (controls[(size_t) i].id == id) return i;
    return -1;
}

void Engine::set (int i, float v, double glideBeats)
{
    if (i < 0 || i >= cx::kCount) return;
    v = juce::jlimit (0.0f, 1.0f, v);
    // what a snapshot holds, and the mutes and solos (they are not held, but they change what is heard)
    const bool muteLike = i < cx::kRet ? (i % cx::kPerLane) >= cx::kMute : (i >= cx::kRetMS && i < cx::kRetMS + 4);
    if (targetV[(size_t) i].load() != v && (controls[(size_t) i].snapshot || muteLike)) stateMoved();
    targetV[(size_t) i] = v;
    glideV[(size_t) i] = (float) glideBeats;
    seqV[(size_t) i].fetch_add (1);
    if (! prepared) { shownV[(size_t) i] = targetV[(size_t) i].load(); }
}

juce::String Engine::valueText (int i) const
{
    const auto& c = controls[(size_t) i];
    const float v = target (i);
    if (i == cx::kTempo) return juce::String (tempoFor (v), 1);
    if (c.kind == Kind::List && c.steps() > 0) return c.choices[juce::jlimit (0, c.steps() - 1, (int) std::lround (v * (float) (c.steps() - 1)))];
    if (c.kind == Kind::Toggle || c.kind == Kind::Hold) return v > 0.5f ? "ON" : "OFF";
    return juce::String ((int) std::lround (v * 100.0f));
}

// ------------------------------------------------------------------------------------- patterns
Pattern Engine::defaultPattern (int l)
{
    // step, velocity, gate, ratchet, note, octave (the last three optional)
    struct S { int step; float v, g; int r = 1, n = 0, o = -1; };
    static const std::vector<S> P[kLanes] = {
        { { 1, 1, .5f }, { 5, 1, .5f }, { 9, 1, .5f }, { 13, 1, .5f } },
        { { 1, .5f, .25f }, { 3, .8f, .25f }, { 5, .5f, .25f }, { 7, .8f, .25f }, { 9, .5f, .25f }, { 11, .8f, .25f }, { 13, .5f, .25f }, { 15, .8f, .25f }, { 16, .35f, .25f } },
        { { 3, .8f, .6f }, { 7, .8f, .6f }, { 11, .8f, .6f }, { 15, .9f, .75f } },
        { { 5, .9f, .4f }, { 13, .9f, .4f } },
        { { 4, .6f, .3f }, { 7, .45f, .3f }, { 12, .7f, .3f }, { 15, .5f, .3f } },
        { { 2, .6f, .5f }, { 3, .75f, .5f }, { 4, .6f, .5f }, { 6, .6f, .5f }, { 7, .75f, .5f }, { 8, .6f, .5f }, { 10, .6f, .5f }, { 11, .75f, .5f }, { 12, .6f, .5f }, { 14, .6f, .5f }, { 15, .75f, .5f }, { 16, .6f, .5f } },
        {},
        { { 1, .7f, .5f }, { 4, 1, .5f }, { 7, .7f, 1.25f, 1, 3 }, { 8, .7f, .5f }, { 10, .7f, .5f }, { 11, 1, 2.5f, 1, 7 }, { 14, .7f, 1.25f, 1, 10, 2 }, { 15, .7f, .6f, 2 }, { 16, .55f, .6f, 3 } },
        { { 3, .8f, .3f }, { 11, .8f, .3f } },
        { { 1, .6f, 16.25f } },
        { { 9, .8f, 4 } },
    };
    Pattern p;
    const int oct = lanes()[(size_t) l].octave;
    for (auto& s : p.steps) s.octave = oct;
    for (auto& s : P[l])
    {
        auto& st = p.steps[(size_t) (s.step - 1)];
        st.on = true; st.vel = s.v; st.gate = s.g; st.ratchet = s.r; st.note = s.n; st.octave = s.o < 0 ? oct : s.o;
    }
    return p;
}

Pattern Engine::pattern (int l) const
{
    const juce::SpinLock::ScopedLockType g (const_cast<juce::SpinLock&> (patLock));
    return lane[(size_t) l].shared;
}

void Engine::setPattern (int l, const Pattern& p, bool onBarLine)
{
    stateMoved();
    auto& ln = lane[(size_t) l];
    {
        const juce::SpinLock::ScopedLockType g (patLock);
        ln.shared = p;
    }
    if (onBarLine && prepared && running.load()) { ln.patDirty = false; ln.patWait = true; }
    else { ln.patWait = false; ln.patDirty = true; }
}

// ----------------------------------------------------------------------------------------- MIDI
void Engine::noteIn (const juce::MidiMessage& m, double arrivedMs)
{
    if (! m.isNoteOnOrOff() || m.getChannel() < 1 || m.getChannel() > kLanes) return;
    NoteEv e;
    const auto* d = m.getRawData();
    e.b[0] = d[0]; e.b[1] = d[1]; e.b[2] = d[2];
    e.ms = arrivedMs;
    const juce::SpinLock::ScopedLockType g (noteWrite);      // one writer at a time: several inputs, several threads
    const auto w = noteFifo.write (1);
    if (w.blockSize1 > 0) noteQ[(size_t) w.startIndex1] = e;
}

void Engine::noteInAt (const juce::MidiMessage& m, int sample)
{
    if (! m.isNoteOnOrOff() || m.getChannel() < 1 || m.getChannel() > kLanes) return;
    NoteEv e;
    const auto* d = m.getRawData();
    e.b[0] = d[0]; e.b[1] = d[1]; e.b[2] = d[2];
    e.at = std::max (0, sample);
    const juce::SpinLock::ScopedLockType g (noteWrite);
    const auto w = noteFifo.write (1);
    if (w.blockSize1 > 0) noteQ[(size_t) w.startIndex1] = e;
}

void Engine::clockIn (juce::uint8 byte, double arrivedMs)
{
    if (byte != 0xF8 && byte != 0xFA && byte != 0xFB && byte != 0xFC) return;
    const juce::SpinLock::ScopedLockType g (clockInWrite);
    const auto w = clockInFifo.write (1);
    if (w.blockSize1 > 0) clockInQ[(size_t) w.startIndex1] = { arrivedMs, byte };
}

// What came from the outside clock since the last block. Returns whether this block follows it.
bool Engine::followClock (double aheadMs)
{
    auto& f = fol;
    const bool on = followOn.load();
    for (;;)
    {
        const auto r = clockInFifo.read (1);
        if (r.blockSize1 <= 0) break;
        const ClockIn m = clockInQ[(size_t) r.startIndex1];
        if (! on) continue;
        if (m.byte == 0xFA)      { f.arm = true; f.top = true; }            // START: the next tick is bar 1's first
        else if (m.byte == 0xFB) { f.arm = true; f.top = false; }           // CONTINUE: on from where it stopped, at the next tick
        else if (m.byte == 0xFC) { f.arm = false; f.rolling = false; running = false; }
        else
        {
            const bool fresh = ! f.have || m.ms - f.lastArr > 250.0;
            if (fresh || f.arm)
            {
                // The ticks begin, or the sender starts (and may start its ticks afresh): the line
                // is laid through this tick, at the tempo last known.
                if (fresh || f.per <= 0.0)
                {
                    f.per = 60000.0 / (24.0 * tempoFor (cur[(size_t) cx::kTempo]));
                    f.bpmSlow = f.bpmSaid = 60000.0 / (24.0 * f.per);
                }
                f.t0 = m.ms; f.t1 = m.ms + f.per; f.count = 0; f.big = 0; f.have = true;
                if (f.arm)
                {
                    f.idx = f.top ? 0 : f.idx + 1;
                    f.rolling = true; f.arm = false; running = true;
                }
                else if (wasRunning)
                {
                    // Already playing when the ticks came: this one is taken for the nearest to
                    // where the rig is, so nothing jumps.
                    f.idx = (juce::int64) std::llround ((ppq + (m.ms - prevBlockMs - aheadMs) * blockBpm / 60000.0) * 24.0);
                    f.rolling = true;
                }
                else { f.idx = 0; f.rolling = false; }
            }
            else
            {
                // Narrow once it has settled, so jitter is averaged out; wide while it settles and
                // when the ticks are plainly somewhere else (the sender's tempo was turned). One
                // tick far out on its own is taken for a late delivery and mostly left alone.
                const double e = m.ms - f.t1;
                f.big = e > 1.5 ? std::max (1, f.big + 1) : e < -1.5 ? std::min (-1, f.big - 1) : 0;
                const bool wide = f.count < 48 || std::abs (f.big) >= 2;
                const double w = juce::MathConstants<double>::twoPi * (wide ? 3.0 : 1.2) * f.per / 1000.0, eu = wide ? e : juce::jlimit (-3.0, 3.0, e);
                f.t0 = f.t1;
                f.t1 += std::sqrt (2.0) * w * eu + f.per;
                f.per = juce::jlimit (60000.0 / (24.0 * 400.0), 60000.0 / (24.0 * 20.0), f.per + w * w * eu);
                if (f.rolling) ++f.idx;
            }
            f.lastArr = m.ms; ++f.count;
            // The tempo said to the instruments (their echo times hang on it) and shown: slow, and
            // on the tenth of a bpm the sender is set to once it is plainly that, so it does not wander.
            const double inst = 60000.0 / (24.0 * f.per);
            f.bpmSlow += (f.count < 48 || std::abs (inst - f.bpmSlow) > 0.15 ? 0.2 : 0.02) * (inst - f.bpmSlow);
            const double tenth = std::round (f.bpmSlow * 10.0) / 10.0, want = std::abs (tenth - f.bpmSlow) < 0.04 ? tenth : f.bpmSlow;
            if (std::abs (want - f.bpmSaid) > 0.049 || (want == tenth && f.bpmSaid != tenth && std::abs (want - f.bpmSaid) > 0.02)) f.bpmSaid = want;
        }
    }
    if (! on) { f = Follow(); locked = false; return false; }
    if (f.have && blockMs - f.lastArr > 250.0) { f.have = false; f.rolling = false; }      // the clock has gone: on alone, at its tempo
    locked = f.have;
    if (f.have)
    {
        followBpm = (float) f.bpmSaid;
        // the TEMPO control is the sender's tempo: shown, kept, and what the rig carries on at alone
        if (const float v = tempoToValue (f.bpmSaid); targetV[(size_t) cx::kTempo].load() != v) set (cx::kTempo, v);
    }
    return f.have;
}

// The headphones' clip for this buffer. In time with the clock: where the clip is read follows the
// rig's place in the bar, set back by what the lanes and the master hold the set back, so it sits
// on the set as the set is heard.
void Engine::renderCue (int n, bool run, double ppq0, double beats)
{
    cueBuf.clear (0, n);
    const juce::SpinLock::ScopedTryLockType hold (cueLock);
    if (! hold.isLocked()) return;
    const CueClip* want = cueWant.load();
    if (cueNow == nullptr && want == nullptr) { cueGain = 0.0f; return; }
    const float step = 1.0f / (float) (0.01 * sr);
    const double perSample = beats / (double) n, held = (double) (maxLat.load() + masterLat.load()) * perSample;
    float* L = cueBuf.getWritePointer (0); float* R = cueBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        if (want != cueNow && cueGain <= 0.0f) { cueNow = want; cuePos = 0.0; }       // one clip out before the next comes in
        if (cueNow == nullptr) { cueGain = 0.0f; break; }
        const float target = want == cueNow ? 1.0f : 0.0f;
        cueGain = target > cueGain ? std::min (target, cueGain + step) : std::max (target, cueGain - step);
        const auto& a = cueNow->audio;
        const int len = a.getNumSamples();
        if (len < 2) continue;
        double pos = cuePos;
        if (run && cueNow->bars > 0 && beats > 0.0)
        {
            const double span = (double) cueNow->bars * 4.0;
            double q = std::fmod (ppq0 + perSample * (double) i - held, span);
            if (q < 0.0) q += span;
            pos = q / span * (double) len;
            cuePos = pos;
        }
        else
        {
            // as it was kept: at its own speed, whatever rate the interface runs at now (a clip kept at 96 kHz played an octave down at 48)
            cuePos += cueNow->rate > 1.0 ? cueNow->rate / sr : 1.0;
            while (cuePos >= (double) len) cuePos -= (double) len;
        }
        const int i0 = juce::jlimit (0, len - 1, (int) pos), i1 = i0 + 1 < len ? i0 + 1 : 0;
        const float f = (float) (pos - (double) i0);
        const float* l = a.getReadPointer (0); const float* r = a.getReadPointer (a.getNumChannels() > 1 ? 1 : 0);
        L[i] = (l[i0] + (l[i1] - l[i0]) * f) * cueGain;
        R[i] = (r[i0] + (r[i1] - r[i0]) * f) * cueGain;
    }
}

// PHONES: the set to the left ear and the cue to the right while a cue plays (or is fading away),
// coming in and going out over 10 ms.
void Engine::phonesSplit (float* left, float* right, int n)
{
    const float target = phonesOn.load() && (cueWant.load() != nullptr || cueNow != nullptr) ? 1.0f : 0.0f;
    if (target <= 0.0f && splitGain <= 0.0f) return;
    const float step = 1.0f / (float) (0.01 * sr);
    const float* cl = cueBuf.getReadPointer (0); const float* cr = cueBuf.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        splitGain = target > splitGain ? std::min (target, splitGain + step) : std::max (target, splitGain - step);
        const float set = 0.5f * (left[i] + right[i]), cue = 0.5f * (cl[i] + cr[i]);
        left[i] += (set - left[i]) * splitGain;
        right[i] = right[i] * (1.0f - splitGain) + cue * splitGain;
    }
}

// The last bars from the tape. Read behind the audio thread's place: what is copied here was
// written whole buffers ago and is not written again for many seconds.
bool Engine::lastHeard (Heard& h, int wantBars) const                          { return heard (h, wantBars, false, 0); }
bool Engine::heldHeard (Heard& h, juce::uint32 stamp, int wantBars) const      { return heard (h, wantBars, true, stamp); }


bool Engine::heard (Heard& h, int wantBars, bool heldOnly, juce::uint32 stamp) const
{
    h = Heard();
    h.sampleRate = sr;
    for (auto& d : h.laneDb) d = -100.0f;
    h.retDb = { -100.0f, -100.0f };
    if (! prepared) return false;
    const juce::int64 end = tapeEnd.load();
    const int count = markCount.load();
    juce::int64 from = 0, to = 0;
    const Mark* m0 = nullptr; const Mark* m1 = nullptr;
    int bars = 0;
    if (count >= 2)
    {
        int last = count - 1;
        if (marks[(size_t) (last % kMarks)].sample > end) --last;          // (that bar line's sound has not left the master chain yet)
        auto held = [&] (int line) { const auto& m = marks[(size_t) (line % kMarks)]; return m.steady && m.stamp == stamp; };
        // held bars only: back to the newest bar that was held in that state
        if (heldOnly) while (last >= 1 && last - 1 > count - kMarks && ! held (last)) --last;
        if (last >= 1 && (! heldOnly || held (last)))
        {
            const auto& e1 = marks[(size_t) (last % kMarks)];
            // (how many bars of this length the tape can hold: fewer than four are then all there will ever be)
            const juce::int64 barLen = e1.sample - marks[(size_t) ((last - 1) % kMarks)].sample;
            if (barLen > 0) h.most = juce::jlimit (1, 4, (int) ((kTape - 65536) / barLen));
            int k = 0;
            // (as many bars as the tape still holds: it is long enough for four at any tempo of the rig's own)
            while (k < wantBars && last - (k + 1) >= 0 && last - (k + 1) > count - kMarks
                   && marks[(size_t) ((last - (k + 1)) % kMarks)].run == e1.run && (! heldOnly || held (last - k))
                   && end - marks[(size_t) ((last - (k + 1)) % kMarks)].sample <= kTape - 65536) ++k;
            // whole bars, if the last of them ended within the last eight seconds (the clock runs, or has only just stopped)
            if (k >= 1 && (heldOnly || end - e1.sample < (juce::int64) (sr * 8.0)))
            {
                m1 = &e1; m0 = &marks[(size_t) ((last - k) % kMarks)];
                bars = k; from = m0->sample; to = e1.sample;
            }
        }
    }
    if (bars == 0 && heldOnly) return false;
    if (bars == 0) { to = end; from = std::max<juce::int64> (0, end - (juce::int64) (sr * 8.0)); }
    const juce::int64 len64 = to - from;
    if (len64 < (juce::int64) (sr * 0.5) || len64 > kTape - 65536 || end - from > kTape - 65536) return false;
    const int len = (int) len64;
    h.audio.setSize (2, len);
    const int at = (int) (from & (kTape - 1)), first = std::min (len, kTape - at);
    for (int ch = 0; ch < 2; ++ch)
    {
        h.audio.copyFrom (ch, 0, tape[(size_t) ch].data() + at, first);
        if (first < len) h.audio.copyFrom (ch, first, tape[(size_t) ch].data(), len - first);
    }
    // (with no bar lines to go by, an effect's hiss or a tail dying away is not a sound to keep: the
    // stopped rig idles at -40 dB, the lead's lane)
    if (h.audio.getMagnitude (0, len) < 1.0e-4f) return false;
    if (bars == 0 && h.audio.getRMSLevel (0, 0, len) < 0.03f) return false;
    if (bars > 0)
    {
        h.bars = bars;
        h.bpm = (double) bars * 4.0 * 60.0 * sr / (double) len;
        auto db = [len] (double q) { return q > 0.0 ? (float) std::max (-100.0, 10.0 * std::log10 (q / (2.0 * (double) len))) : -100.0f; };
        double all = 0.0;
        for (int l = 0; l < kLanes + 2; ++l)
        {
            const double q = m1->sq[(size_t) l] - m0->sq[(size_t) l];
            all += q;
            if (l < kLanes) h.laneDb[(size_t) l] = db (q); else h.retDb[(size_t) (l - kLanes)] = db (q);
        }
        h.mixDb = db (all);
    }
    return true;
}

bool Engine::nextClock (ClockMsg& m)
{
    const auto r = clockFifo.read (1);
    if (r.blockSize1 <= 0) return false;
    m = clockQ[(size_t) r.startIndex1];
    return true;
}

void Engine::pushClock (juce::uint8 byte, double dueMs, juce::int64 sample)
{
    const auto w = clockFifo.write (1);
    if (w.blockSize1 > 0) clockQ[(size_t) w.startIndex1] = { dueMs, sample, byte };
}

// The notes that came since the last block, each at the place in this block that matches when it
// came (the block before this one's span of time), so what was played evenly sounds evenly.
void Engine::gatherNotes (int n, double spb)
{
    liveMidi.clear();
    const double span = std::max (0.05, blockMs - prevBlockMs);
    auto place = [&] (const NoteEv& e) -> bool
    {
        int at = e.at;
        if (at < 0)
        {
            if (e.ms >= blockMs) return false;                      // after this block began: the next one's
            at = (int) std::floor ((e.ms - prevBlockMs) / span * (double) n);
        }
        at = juce::jlimit (0, n - 1, at);
        liveMidi.addEvent (juce::MidiMessage (e.b[0], e.b[1], e.b[2]), at);
        if ((e.b[0] & 0xf0) == 0x90 && e.b[2] > 0 && e.at < 0 && wasRunning)
        {
            // how far from the nearest sixteenth it lands, for setting the clock's LEAD
            const double q = ppq + (double) at / spb, off = (q - std::round (q / kStepBeats) * kStepBeats) * 60000.0 / blockBpm;
            if (std::abs (off) < 40.0)
            {
                const int c = lateCount.load();
                lateMs = c == 0 ? (float) off : lateMs.load() + 0.08f * ((float) off - lateMs.load());
                lateCount = c + 1;
            }
        }
        return true;
    };
    int keep = 0;
    for (int i = 0; i < noteKeptN; ++i) if (! place (noteKept[(size_t) i])) noteKept[(size_t) keep++] = noteKept[(size_t) i];
    noteKeptN = keep;
    for (;;)
    {
        const auto r = noteFifo.read (1);
        if (r.blockSize1 <= 0) break;
        const NoteEv e = noteQ[(size_t) r.startIndex1];
        if (! place (e) && noteKeptN < (int) noteKept.size()) noteKept[(size_t) noteKeptN++] = e;
    }
}

// -------------------------------------------------------------------------------------- inserts
int Engine::leaving() const
{
    int n = 0;
    for (auto& ln : lane) { for (auto& o : ln.out) n += o.load() != nullptr ? 1 : 0; n += ln.voiceOut.load() != nullptr ? 1 : 0; }
    return n;
}

void Engine::setInsertType (int l, int slot, int type, bool defaults, const SetupState* setup, double fadeBeats, const float* cells)
{
    stateMoved();
    type = juce::jlimit (-1, (int) effects().size() - 1, type);
    auto& ln = lane[(size_t) l];
    collectRetired (false);
    InsertInst* inst = nullptr;
    if (type >= 0)
    {
        const auto& d = effects()[(size_t) type];
        // the files this effect holds in this slot (dropped on it, or a snapshot's)
        const auto& held = insFiles[(size_t) l][(size_t) slot];
        const bool filed = ! held.empty() && held.front().type == type;
        // One that already holds them, taken out earlier or made ahead: it goes back in with its
        // places loaded. Otherwise a new one, which reads them from the disk.
        if (filed) inst = takeParked (l, slot, type, held);
        const bool reused = inst != nullptr;
        if (! reused)
        {
            inst = new InsertInst();
            inst->type = type; inst->lane = l; inst->slot = slot;
            inst->proc = makeProc (d.module, d.fixed);
        }
        else
        {
            // as a new one would be: every parameter where the effect starts it (its places stay)
            for (int i = 0; i < inst->proc->numModuleParams(); ++i) inst->proc->param (i)->setValueNotifyingHost (inst->proc->param (i)->getDefaultValue());
            for (auto& f : d.fixed) if (const int i = inst->proc->paramIndex (f.param); i >= 0) inst->proc->param (i)->setValueNotifyingHost (inst->proc->param (i)->convertTo0to1 (f.value));
        }
        inst->mixCell = -1; inst->dryWas = 1.0f; inst->gWas = 1.0f; inst->soloOn = false;
        for (int c = 0; c < kCells; ++c)
        {
            const int i = cx::ins (l, slot, c);
            auto& ct = controls[(size_t) i];
            const juce::String id = ct.id;
            if (d.cells[c].label != nullptr) { inst->bind[c] = makeBind (*inst->proc, d.cells[c], &ct); ct.morph = false; }      // (its knobs glide under the hand; no snapshot knob reaches them)
            else { ct.used = false; ct.label = {}; ct.choices.clear(); ct.kind = Kind::Knob; ct.def = 0.0f; ct.glides = false; }
            ct.id = id; ct.snapshot = true;
            if (defaults) set (i, ct.def);
            else applyBind (inst->bind[c], cells != nullptr && cells[c] >= 0.0f ? cells[c] : target (i));
            if (d.cells[c].label != nullptr && juce::String (d.cells[c].param) == "mix") inst->mixCell = c;
        }
        if (inst->proc->paramIndex ("dry") >= 0) inst->dryRaw = inst->proc->apvts.getRawParameterValue ("dry");
        // its CONFIG before it is prepared: a convolution builds its first room for the knobs as they stand
        if (setup != nullptr)
        {
            std::vector<std::string> bound;
            for (auto& cell : d.cells) if (cell.param != nullptr) bound.push_back (cell.param);
            applySetupTo (*inst->proc, bound, *setup);
        }
        if (prepared) { inst->proc->setRateAndBufferSizeDetails (sr, maxBlock); inst->proc->prepareToPlay (sr, maxBlock); }
        if (filed && ! reused) { replayFiles (*inst->proc, held); inst->files = held; }
    }
    else
        for (int c = 0; c < kCells; ++c)
        {
            auto& ct = controls[(size_t) cx::ins (l, slot, c)];
            ct.used = false; ct.label = {}; ct.choices.clear(); ct.kind = Kind::Knob; ct.glides = false;
        }
    insType[(size_t) l][(size_t) slot] = type;
    // The one that was there leaves over the fade while this one arrives. (One still leaving from an
    // earlier change is let go at once.)
    auto* old = ln.ins[slot].exchange (inst);
    if (auto* earlier = ln.out[slot].exchange (prepared ? old : nullptr)) retired.push_back ({ earlier, blockCount.load() });
    if (! prepared && old != nullptr) retired.push_back ({ old, blockCount.load() });
    ln.fadeBeats[slot] = (float) fadeBeats;
    ln.fadeSeq[slot].fetch_add (1);
    // (the knobs' own places follow, now that the new effect is the one they reach)
    if (cells != nullptr && type >= 0 && ! defaults) for (int c = 0; c < kCells; ++c) if (cells[c] >= 0.0f) set (cx::ins (l, slot, c), cells[c]);
    collectRetired (false);
    updateLatency();
}

void Engine::forkVoice (int l, double fadeBeats)
{
    auto& ln = lane[(size_t) l];
    const auto& d = lanes()[(size_t) l];
    auto* was = ln.voice.load();
    auto* v = new Voice();
    v->proc = makeProc (d.module, d.fixed);
    // as the one playing stands, to the last parameter (CONFIG's too), with the file it plays
    const int np = (int) was->proc->allSpecs().size();
    for (int i = 0; i < np; ++i)
        if (auto* from = was->proc->param (i))
            if (auto* to = v->proc->param (i))
                if (std::abs (to->getValue() - from->getValue()) > 1.0e-7f) to->setValueNotifyingHost (from->getValue());
    for (int k = 0; k < kLaneKnobs; ++k) v->bind[k] = makeBind (*v->proc, d.knobs[k], nullptr);
    if (files[(size_t) l].isNotEmpty()) v->proc->mod().loadFiles (juce::StringArray (files[(size_t) l]));
    if (prepared) { v->proc->setRateAndBufferSizeDetails (sr, maxBlock); v->proc->prepareToPlay (sr, maxBlock); }
    stateMoved();
    ln.voice.store (v);
    if (auto* earlier = ln.voiceOut.exchange (prepared ? was : nullptr)) retiredVoices.push_back ({ earlier, blockCount.load() });
    if (! prepared) retiredVoices.push_back ({ was, blockCount.load() });
    ln.fadeBeats[kSlots] = (float) fadeBeats;
    ln.fadeSeq[kSlots].fetch_add (1);
    collectRetired (false);
    updateLatency();
}

// Audio thread, once a block for each stage of a lane: where its fade stands at the block's start
// (g0) and end (g1), from 0 (all the one leaving) to 1 (all the one that took its place). False:
// nothing is fading. A fade sets off with the knobs of a recall: on the next beat while the clock
// runs, and over the same time. Never shorter than a fiftieth of a second.
bool Engine::fadeStep (Lane& ln, int st, double beats, float& g0, float& g1)
{
    const juce::uint32 q = ln.fadeSeq[st].load();
    if (q != ln.fadeSeen[st])
    {
        ln.fadeSeen[st] = q;
        const double now = gClock - beats, least = 0.02 * tempoFor (cur[(size_t) cx::kTempo]) / 60.0, asked = (double) ln.fadeBeats[st].load();
        ln.fadeLen[st] = std::max (asked, least);
        ln.fadeStart[st] = asked > least && wasRunning ? now + (std::ceil (ppq - 1e-9) - ppq) : now;
        ln.fadeG[st] = 0.0f;
        ln.fading[st] = true;
    }
    if (! ln.fading[st]) return false;
    g0 = ln.fadeG[st];
    const double t = (gClock - ln.fadeStart[st]) / ln.fadeLen[st];
    g1 = (float) juce::jlimit (0.0, 1.0, t);
    ln.fadeG[st] = g1;
    ln.fadeAt[st].store (g1, std::memory_order_relaxed);
    if (t >= 1.0) { ln.fading[st] = false; ln.fadeDoneAt[st].store (blockCount.load()); ln.fadeDone[st].store (q); }
    return true;
}

void Engine::collectRetired (bool all)
{
    // An insert taken out is let go once the audio thread has gone two blocks without it: deleted,
    // or kept aside if it holds files.
    const auto now = blockCount.load();
    // one that has finished leaving (the audio thread says which fade is done) is taken off its lane, to be let go below
    // (Once the buffer its fade ended in is over, the audio thread does not touch it again: it can
    // go at once, and an effect holding files is kept aside for the next recall that wants it.)
    for (auto& ln : lane)
    {
        auto gone = [&] (int st) { return all || ! prepared || (ln.fadeDone[st].load() == ln.fadeSeq[st].load() && now > ln.fadeDoneAt[st].load()); };
        for (int k = 0; k < kSlots; ++k)
            if (ln.out[k].load() != nullptr && gone (k))
            {
                auto* was = ln.out[k].exchange (nullptr);
                if (all || was->files.empty()) delete was; else park (was);
            }
        if (ln.voiceOut.load() != nullptr && gone (kSlots)) delete ln.voiceOut.exchange (nullptr);
    }
    for (size_t i = retiredVoices.size(); i-- > 0;)
        if (all || now >= retiredVoices[i].second + 2 || ! prepared)
        {
            delete retiredVoices[i].first;
            retiredVoices.erase (retiredVoices.begin() + (long) i);
        }
    for (size_t i = retired.size(); i-- > 0;)
        if (all || now >= retired[i].second + 2 || ! prepared)
        {
            if (all || retired[i].first->files.empty()) delete retired[i].first;
            else park (retired[i].first);
            retired.erase (retired.begin() + (long) i);
        }
}

Engine::InsertInst* Engine::takeParked (int l, int slot, int type, const std::vector<FileRef>& refs)
{
    for (size_t i = 0; i < parked.size(); ++i)
        if (auto* p = parked[i]; p->lane == l && p->slot == slot && p->type == type && p->files == refs)
        {
            parked.erase (parked.begin() + (long) i);
            return p;
        }
    return nullptr;
}

void Engine::park (InsertInst* inst)
{
    // one per slot and set of files; the oldest go when there are many
    for (size_t i = parked.size(); i-- > 0;)
        if (auto* p = parked[i]; p->lane == inst->lane && p->slot == inst->slot && p->type == inst->type && p->files == inst->files)
        { delete p; parked.erase (parked.begin() + (long) i); }
    parked.push_back (inst);
    while (parked.size() > 12) { delete parked.front(); parked.erase (parked.begin()); }
}

std::vector<Engine::FileRef> Engine::insertFiles (int l, int slot) const
{
    if (auto* in = lane[(size_t) l].ins[slot].load()) return in->files;
    return {};
}

bool Engine::insertHolds (int l, int slot, int type, const std::vector<FileRef>& refs) const
{
    auto* in = lane[(size_t) l].ins[slot].load();
    if (type < 0) return in == nullptr;
    return in != nullptr && in->type == type && in->files == refs;
}

void Engine::warmInsert (int l, int slot, int type, const std::vector<FileRef>& refs)
{
    if (type < 0 || type >= (int) effects().size() || refs.empty() || refs.front().type != type) return;
    collectRetired (false);
    if (insertHolds (l, slot, type, refs)) return;
    for (auto* p : parked) if (p->lane == l && p->slot == slot && p->type == type && p->files == refs) return;
    for (auto& r : retired) if (r.first->lane == l && r.first->slot == slot && r.first->type == type && r.first->files == refs) return;
    const auto& d = effects()[(size_t) type];
    auto* inst = new InsertInst();
    inst->type = type; inst->lane = l; inst->slot = slot;
    inst->proc = makeProc (d.module, d.fixed);
    if (! inst->proc->mod().acceptsFiles()) { delete inst; return; }
    if (prepared) { inst->proc->setRateAndBufferSizeDetails (sr, maxBlock); inst->proc->prepareToPlay (sr, maxBlock); }
    replayFiles (*inst->proc, refs);
    inst->files = refs;
    park (inst);
}

void Engine::updateLatency()
{
    if (! prepared) return;
    int lat[kLanes] = {}, mx = 0;
    for (int l = 0; l < kLanes; ++l)
    {
        lat[l] = lane[(size_t) l].voice.load()->proc->getLatencySamples();
        for (auto& i : lane[(size_t) l].ins) if (auto* in = i.load()) lat[l] += in->proc->getLatencySamples();
        mx = std::max (mx, lat[l]);
    }
    // Every lane is given the longest wait any lane could need, always (holdLat), so the rig's
    // delay does not move with what a recall or a hand puts on a lane.
    mx = std::min (std::max (mx, holdLat), 8000);
    for (int l = 0; l < kLanes; ++l) lane[(size_t) l].delay = juce::jlimit (0, 8000, mx - lat[l]);
    maxLat = mx;
    int ml = 0;
    for (auto& m : master) ml += m->getLatencySamples();
    masterLat = ml;
}

// --------------------------------------------------------------------------------------- files
bool Engine::acceptsFiles (Target t) const
{
    if (t.kind == 2 && t.slot == 1) return true;        // SPACE: a file makes it the convolution
    auto* p = processorFor (t);
    return p != nullptr && p->mod().acceptsFiles();
}

static int placeOf (SFProcessor& p)
{
    return p.paramIndex ("place") >= 0 ? (int) std::lround (p.apvts.getRawParameterValue ("place")->load()) : 0;
}

bool Engine::loadFiles (Target t, const juce::StringArray& paths)
{
    if (! acceptsFiles (t)) return false;
    stateMoved();
    // unreadable (macOS has not been allowed into that folder yet, or the file is gone): say so
    juce::StringArray ok;
    for (auto& path : paths)
    {
        juce::FileInputStream in { juce::File (path) };
        if (in.openedOk()) ok.add (path);
    }
    if (ok.isEmpty()) return false;
    if (t.kind == 2 && t.slot == 1) setSpaceModel (1);
    auto* p = processorFor (t);
    if (p == nullptr) return false;
    if (t.kind == 0)
    {
        p->mod().loadFiles (juce::StringArray (ok[0]));
        files[(size_t) t.lane] = ok[0];
        return true;
    }
    FileRef r;
    r.place = placeOf (*p);
    r.type = t.kind == 1 ? insType[(size_t) t.lane][(size_t) t.slot] : -1;
    r.files = ok;
    p->mod().loadFiles (ok);
    // what was held for that place, or for another effect in this slot, is replaced
    auto& held = t.kind == 1 ? insFiles[(size_t) t.lane][(size_t) t.slot] : spaceFiles;
    held.erase (std::remove_if (held.begin(), held.end(), [&r] (const FileRef& o) { return o.place == r.place || o.type != r.type; }), held.end());
    held.push_back (r);
    if (t.kind == 1) if (auto* in = lane[(size_t) t.lane].ins[t.slot].load()) in->files = held;
    return true;
}

void Engine::replayFiles (SFProcessor& p, const std::vector<FileRef>& refs)
{
    if (! p.mod().acceptsFiles()) return;
    const int pi = p.paramIndex ("place");
    const float was = pi >= 0 ? p.param (pi)->getValue() : 0.0f;
    for (auto& r : refs)
    {
        juce::StringArray ok;
        for (auto& path : r.files) if (juce::File (path).existsAsFile()) ok.add (path);
        if (ok.isEmpty()) continue;
        if (pi >= 0) p.param (pi)->setValueNotifyingHost (p.param (pi)->convertTo0to1 ((float) r.place));
        p.mod().loadFiles (ok);
    }
    if (pi >= 0) p.param (pi)->setValueNotifyingHost (was);
}

const std::vector<Engine::FileRef>& Engine::heldFiles (Target t) const
{
    static const std::vector<FileRef> none;
    if (t.kind == 1) return insFiles[(size_t) t.lane][(size_t) t.slot];
    if (t.kind == 2 && t.slot == 1) return spaceFiles;
    return none;
}

void Engine::setHeldFiles (Target t, const std::vector<FileRef>& refs)
{
    stateMoved();
    if (t.kind == 2 && t.slot == 1) { spaceFiles = refs; replayFiles (*space[1], refs); }
    else if (t.kind == 1)
    {
        // kept for when that effect is next put in the slot; one already there with other files reads these now
        insFiles[(size_t) t.lane][(size_t) t.slot] = refs;
        if (auto* in = lane[(size_t) t.lane].ins[t.slot].load())
            if (! refs.empty() && refs.front().type == in->type && ! (in->files == refs)) { replayFiles (*in->proc, refs); in->files = refs; }
    }
}

void Engine::expectInsertFiles (int l, int slot, const std::vector<FileRef>& refs)
{
    insFiles[(size_t) l][(size_t) slot] = refs;
}

void Engine::setSpaceModel (int model)
{
    model = juce::jlimit (0, kSpaceModels - 1, model);
    if (model == spaceWant.load()) return;
    stateMoved();
    spaceWant = model;
    if (! prepared) spaceNow = model;
}

void Engine::panic() { panicNow = true; }

// ---------------------------------------------------------------------------------------- setup
SFProcessor* Engine::processorFor (Target t) const
{
    if (t.kind == 0) return lane[(size_t) t.lane].voice.load()->proc.get();
    if (t.kind == 1) { auto* in = lane[(size_t) t.lane].ins[t.slot].load(); return in != nullptr ? in->proc.get() : nullptr; }
    return t.slot == 0 ? retA.get() : space[(size_t) spaceWant.load()].get();
}

std::vector<std::string> Engine::boundIds (Target t) const
{
    std::vector<std::string> ids;
    auto add = [&ids] (const KnobDef* k, int n) { for (int i = 0; i < n; ++i) if (k[i].param != nullptr) ids.push_back (k[i].param); };
    if (t.kind == 0) add (lanes()[(size_t) t.lane].knobs, kLaneKnobs);
    else if (t.kind == 1) { const int ty = insType[(size_t) t.lane][(size_t) t.slot]; if (ty >= 0) add (effects()[(size_t) ty].cells, kCells); }
    else add ((t.slot == 0 ? returnA() : returnB (spaceWant.load())).cells, kCells);
    return ids;
}

std::vector<Engine::SetupParam> Engine::setupParams (Target t) const
{
    std::vector<SetupParam> out;
    auto* p = processorFor (t);
    if (p == nullptr) return out;
    if (t.kind == 2 && t.slot == 1)
    {
        // which reverb SPACE is: first in its CONFIG
        SetupParam sp;
        sp.id = kModelId; sp.name = "MODEL"; sp.kind = Kind::List;
        for (int m = 0; m < kSpaceModels; ++m) sp.choices.add (spaceModelName (m));
        sp.value = (float) spaceWant.load() / (float) (kSpaceModels - 1); sp.def = 0.0f; sp.text = spaceModelName (spaceWant.load());
        out.push_back (sp);
    }
    const auto bound = boundIds (t);
    const auto& specs = p->allSpecs();
    for (int i = 0; i < p->numModuleParams(); ++i)
    {
        const auto& s = specs[(size_t) i];
        if (std::find (bound.begin(), bound.end(), s.id) != bound.end()) continue;
        auto* prm = p->param (i);
        SetupParam sp;
        sp.id = s.id; sp.name = s.name; sp.value = prm->getValue(); sp.def = prm->getDefaultValue(); sp.text = prm->getCurrentValueAsText();
        if (s.type == sf::PType::Choice) { sp.kind = Kind::List; for (auto& c : s.choices) sp.choices.add (c); }
        else if (s.type == sf::PType::Toggle) sp.kind = Kind::Toggle;
        else if (s.type == sf::PType::Momentary) sp.kind = Kind::Hold;
        else sp.kind = s.unit == "BIP" || (s.min < 0.0f && s.max > 0.0f && std::abs (s.min + s.max) < 1e-3f) ? Kind::Bipolar : Kind::Knob;
        out.push_back (sp);
    }
    return out;
}

void Engine::setSetup (Target t, const juce::String& id, float v)
{
    stateMoved();
    if (id == kModelId)
    {
        if (t.kind == 2 && t.slot == 1) setSpaceModel ((int) std::lround (juce::jlimit (0.0f, 1.0f, v) * (float) (kSpaceModels - 1)));
        return;
    }
    if (auto* p = processorFor (t))
    {
        const int i = p->paramIndex (id.toStdString());
        if (i < 0 || i >= p->numModuleParams()) return;
        p->param (i)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
        updateLatency();
    }
}

Engine::SetupState Engine::getSetup (Target t) const
{
    SetupState st;
    if (auto* p = processorFor (t))
    {
        // SPACE's setup says which reverb it was read from: it is only ever put back on that one
        if (t.kind == 2 && t.slot == 1) st.push_back ({ kModelId, (float) spaceWant.load() });
        const auto bound = boundIds (t);
        const auto& specs = p->allSpecs();
        for (int i = 0; i < p->numModuleParams(); ++i)
            if (std::find (bound.begin(), bound.end(), specs[(size_t) i].id) == bound.end())
                st.push_back ({ specs[(size_t) i].id, p->param (i)->getValue() });
    }
    return st;
}

void Engine::applySetupTo (SFProcessor& p, const std::vector<std::string>& bound, const SetupState& st)
{
    for (auto& kv : st)
    {
        const int i = p.paramIndex (kv.first.toStdString());
        if (i < 0 || i >= p.numModuleParams() || std::find (bound.begin(), bound.end(), kv.first.toStdString()) != bound.end()) continue;
        auto* prm = p.param (i);
        if (std::abs (prm->getValue() - kv.second) > 1e-6f) prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, kv.second));
    }
}

void Engine::applySetup (Target t, const SetupState& st)
{
    if (auto* p = processorFor (t))
    {
        if (t.kind == 2 && t.slot == 1)
        {
            // Which reverb SPACE is belongs to the rig, not to a snapshot: setup from the other one is left out.
            int from = 0;
            for (auto& kv : st) if (kv.first == kModelId) from = (int) std::lround (kv.second);
            if (from != spaceWant.load()) return;
        }
        applySetupTo (*p, boundIds (t), st);
        updateLatency();
    }
}

// ---------------------------------------------------------------------------------------- audio
void Engine::prepare (double rate, int block)
{
    prepared = false;
    {
        // a recording being made at another rate stops being fed here: nothing at the new rate goes into its files
        const juce::SpinLock::ScopedLockType g (recLock);
        if (recSink != nullptr && std::abs (recSinkRate - rate) > 0.5) recSink = nullptr;
    }
    sr = rate; maxBlock = std::max (16, block);
    // The suite's bus is shared memory: it outlives an engine, and the app. A kick reported on it
    // by an earlier one stays "alive" for four seconds by the clock on the wall, which made the
    // start of a render depend on what had run just before it. An engine starts with none.
    { auto& bd = sf::Bus::get().d(); bd.kickWrite = 0; bd.kickSeenMs = 0; }
    // nothing is part-way between two effects or two instruments when the audio starts: what was leaving is let go
    collectRetired (false);
    for (auto& ln : lane)
        for (int k = 0; k < kStages; ++k)
        {
            ln.fadeSeen[k] = ln.fadeSeq[k].load(); ln.fadeDone[k] = ln.fadeSeen[k];
            ln.fading[k] = false; ln.fadeG[k] = 1.0f; ln.fadeAt[k] = 1.0f;
        }
    auto prep = [this] (SFProcessor& p) { p.setRateAndBufferSizeDetails (sr, maxBlock); p.prepareToPlay (sr, maxBlock); };
    for (auto& ln : lane)
    {
        prep (*ln.voice.load()->proc);
        if (auto* o = ln.voiceOut.load()) prep (*o->proc);
        for (auto& i : ln.out) if (auto* in = i.load()) prep (*in->proc);
        for (auto& i : ln.ins) if (auto* in = i.load()) prep (*in->proc);
        ln.midi.ensureSize (4096);
        ln.patWait = false;
        for (auto& d : ln.dl) d.assign (8192, 0.0f);
        ln.dlPos = 0;
        for (auto& e : ln.eq) e = Biquad();
        for (auto& s : ln.eqSet) s = 99.0f;
        ln.patDirty = true;
    }
    // the longest wait a lane could need: its instrument's, and the slowest effect in every slot
    int inst = 0;
    for (auto& ln : lane) inst = std::max (inst, ln.voice.load()->proc->getLatencySamples());
    holdLat = inst + kSlots * slowestEffect (sr);
    for (auto* pk : parked) prep (*pk->proc);
    prep (*retA);
    for (auto& sp : space) prep (*sp);
    spaceNow = spaceWant.load(); spaceHold = 0;
    for (auto& m : master) prep (*m);
    for (auto* b : { &buf, &mix, &sendA, &sendB, &work, &pre, &ghost, &recBuf, &cueBuf }) b->setSize (2, maxBlock, false, true, false);
    cueBuf.clear(); cueNow = nullptr; cueGain = 0.0f; cuePos = 0.0; splitGain = 0.0f;
    kTape = std::max (1 << 20, juce::nextPowerOfTwo ((int) std::ceil (sr * 16.0) + 65536 + maxBlock));
    for (auto& t : tape) t.assign ((size_t) kTape, 0.0f);
    tapePos = 0; tapeEnd = 0; markCount = 0; runCount = 0;
    liveMidi.ensureSize (8192);
    wallValid = false; wallMs = -1.0; noteKeptN = 0; clkPpq = 0.0; clkWas = false;
    ghostMidi.ensureSize (4096);            // (a voice leaving is given its lane's notes: room for them before the audio starts)
    fol = Follow();
    loud->m.prepare (sr);
    prepared = true;
    updateLatency();
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Engine::getPosition() const
{
    PositionInfo p;
    p.setBpm (blockBpm);
    p.setTimeSignature (TimeSignature { 4, 4 });
    p.setIsPlaying (wasRunning);
    p.setPpqPosition (ppq);
    p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
    p.setTimeInSamples (samplesDone);
    return p;
}

void Engine::advanceControls (double beats)
{
    gClock += beats;
    glidingNow = false;
    for (int i = 0; i < cx::kCount; ++i)
    {
        const auto s = (size_t) i;
        const uint32_t q = seqV[s].load();
        if (q != seen[s])
        {
            seen[s] = q;
            const float len = glideV[s].load();
            gFrom[s] = cur[s]; gTo[s] = targetV[s].load();
            if (len > 0.0f && controls[s].glides)
            {
                // A move sets off on the next beat while the clock runs. One already on its way
                // carries on from where the sound is toward the new place, without waiting again.
                const double now = gClock - beats, fresh = wasRunning ? now + (std::ceil (ppq - 1e-9) - ppq) : now;
                gStart[s] = gliding[s] ? std::max (now, std::min (gStart[s], fresh)) : fresh;
                gLen[s] = len; gliding[s] = true;
            }
            else { cur[s] = gTo[s]; gliding[s] = false; }
        }
        if (gliding[s])
        {
            glidingNow = true;
            const double t = (gClock - gStart[s]) / gLen[s];
            if (t >= 1.0) { cur[s] = gTo[s]; gliding[s] = false; }
            else if (t > 0.0) cur[s] = gFrom[s] + (gTo[s] - gFrom[s]) * (float) t;
        }
        if (cur[s] != applied[s])
        {
            applied[s] = cur[s];
            shownV[s] = cur[s];
            if (bind[s].raw != nullptr) applyBind (bind[s], cur[s]);
            else if (i < cx::kRet)
            {
                const int k = i % cx::kPerLane;
                if (k < kLaneKnobs)
                {
                    auto& ln = lane[(size_t) (i / cx::kPerLane)];
                    if (auto* v = ln.voice.load()) applyBind (v->bind[k], cur[s]);
                    // one on its way out follows the knobs that travel; a menu or a switch stays as it left it
                    if (controls[s].glides && ln.fading[kSlots]) if (auto* o = ln.voiceOut.load()) applyBind (o->bind[k], cur[s]);
                }
                else if (k >= cx::kIns && k < cx::kGen)
                    if (auto* in = lane[(size_t) (i / cx::kPerLane)].ins[(k - cx::kIns) / kCells].load())
                        if (! (in->soloOn && (k - cx::kIns) % kCells == in->mixCell))
                            applyBind (in->bind[(k - cx::kIns) % kCells], cur[s]);
            }
        }
    }
}

void Engine::setEq (Lane& ln, int band, float db)
{
    // LOW: a shelf below 200 Hz. MID: a wide bell at 700 Hz. HIGH: a shelf above 2.5 kHz.
    static const double f0[] = { 200.0, 700.0, 2500.0 };
    const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * f0[band] / sr, cs = std::cos (w), sn = std::sin (w);
    double b0, b1, b2, a0, a1, a2;
    if (band == 1)
    {
        const double al = sn / (2.0 * 0.6);
        b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A;
    }
    else
    {
        const double al = sn / 2.0 * std::sqrt (2.0), rt = 2.0 * std::sqrt (A) * al, sg = band == 0 ? 1.0 : -1.0;
        b0 = A * ((A + 1) - sg * (A - 1) * cs + rt);
        b1 = sg * 2 * A * ((A - 1) - sg * (A + 1) * cs);
        b2 = A * ((A + 1) - sg * (A - 1) * cs - rt);
        a0 = (A + 1) + sg * (A - 1) * cs + rt;
        a1 = -sg * 2 * ((A - 1) + sg * (A + 1) * cs);
        a2 = (A + 1) + sg * (A - 1) * cs - rt;
    }
    auto& e = ln.eq[band];
    e.b0 = (float) (b0 / a0); e.b1 = (float) (b1 / a0); e.b2 = (float) (b2 / a0); e.a1 = (float) (a1 / a0); e.a2 = (float) (a2 / a0);
}

// An EQ knob: centre is flat, down cuts to -24 dB, up lifts to +9 dB.
static float eqDbFor (float v) { return v < 0.5f ? -24.0f * (1.0f - 2.0f * v) : 9.0f * (2.0f * v - 1.0f); }

void Engine::render (juce::AudioBuffer<float>& out)
{
    const int n = out.getNumSamples();
    jassert (prepared && n <= maxBlock && out.getNumChannels() >= 2);
    const juce::ScopedNoDenormals noDenormals;
    auto clock = [this] { return profile ? juce::Time::getHighResolutionTicks() : (juce::int64) 0; };
    auto since = [this] (juce::int64 t) { return profile ? juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t) : 0.0; };
    const auto tAll = clock();
    // A recording under way: held for the whole buffer, so stopping it waits for the buffer's end.
    const juce::SpinLock::ScopedTryLockType recHold (recLock);
    RecSink* const rec = recHold.isLocked() ? recSink : nullptr;
    if (rec != nullptr) rec->begin (n);
    // When this block is on the wall clock, smoothed, so MIDI in and the clock out keep an even
    // pace however unevenly the device calls. (A test has no device: its clock is the samples.)
    const double durMs = (double) n * 1000.0 / sr, nowMs = wallMs >= 0.0 ? wallMs : virtualMs;
    wallMs = -1.0; virtualMs += durMs;
    if (! wallValid || std::abs (nowMs - (blockMs + lastDurMs)) > 30.0) { blockMs = nowMs; prevBlockMs = nowMs - durMs; wallValid = true; }
    else { prevBlockMs = blockMs; blockMs += lastDurMs + 0.1 * (nowMs - (blockMs + lastDurMs)); }
    lastDurMs = durMs;

    // How long a note from outside takes to be heard: a block's wait, the interface, the look-ahead inside.
    const double waitNow = devBufMs > 0.0 ? devBufMs : durMs, insideNow = (double) (maxLat.load() + masterLat.load()) * 1000.0 / sr;
    waitMs = (float) waitNow; insideMs = (float) insideNow;
    // An outside clock, if one is followed: its START and STOP are the transport's, its tempo the
    // rig's. The rig's own steps are played ahead of it by that time (or not), and by TRIM.
    const double ahead = (followBeat.load() ? waitNow + (double) outMs.load() + insideNow : 0.0) + (double) followTrim.load();
    const bool led = followClock (ahead);
    const bool run = running.load();
    blockBpm = led ? fol.bpmSaid : tempoFor (cur[(size_t) cx::kTempo]);
    double beats = (double) n * blockBpm / 60.0 / sr;
    const bool starting = run && ! wasRunning, stopping = ! run && wasRunning;
    bool again = false;                                 // the outside clock went back to the start while this was playing

    // MIDI clock out. A follower's note for a beat has to be back here a block before that beat
    // is rendered, so the clock runs ahead of the sound: two blocks (one for the sender to be on
    // time, one for the note's way in) and LEAD, the follower's own delay. RUN waits that long.
    // Ticks go out only while it runs, as Live's did: the OXI One is set up for a clock that
    // starts with START, and falls back to its own when there is none. None while following.
    const bool clk = clockOn.load() && ! followOn.load();
    const double lookBeats = clk ? (2.0 * durMs + (double) clockLead.load()) * blockBpm / 60000.0 : 0.0;
    if (starting) ppq = -lookBeats;
    if (led && run)
    {
        // Where the sender's clock is. A block holds what came during the block before it (a note
        // from outside sounds at its own place in the next block), so with nothing ahead this
        // block is the stretch of the sender's clock from the last block's time to this one's.
        auto& f = fol;
        if (! f.rolling) { f.idx = -1; f.rolling = true; }                  // RUN by hand, the sender stopped: from the top, on its next tick
        // Up to the tick it expects next, the line between the last tick and that one; past it (the
        // head start reaches a tick or two on), the steadier period alone.
        const double slope = std::max (1.0, f.t1 - f.t0), per = std::max (1.0, f.per);
        auto at = [&] (double ms) { return (ms <= f.t1 ? (double) f.idx + (ms - f.t0) / slope : (double) f.idx + 1.0 + (ms - f.t1) / per) / 24.0; };
        const double from = at (prevBlockMs + ahead), end = at (blockMs + ahead);
        const bool jump = ! starting && std::abs (from - ppq) > 0.25;      // the sender went somewhere else (START again, most likely)
        if (starting || jump)
        {
            // It sets off from where the sender is now, so no step is skipped: what should already
            // have been played ahead is played at once (bar 1's first step, after START).
            const double here = at (prevBlockMs);
            ppq = here < 0.5 ? std::min (0.0, here) : here;
            again = jump;
        }
        beats = juce::jlimit (0.0, 0.5, end - ppq);
    }
    const double spb = beats > 1.0e-9 ? (double) n / beats : sr * 60.0 / blockBpm;
    if (clk)
    {
        const double base = blockMs + durMs;
        if (starting) { clkPpq = 0.0; pushClock (0xFA, base - 1.0, samplesDone); }       // START, a millisecond before its first tick
        if (stopping) pushClock (0xFC, base, samplesDone);
        // Switched on with the rig already running (SEND pressed, a sequencer plugged in, the audio
        // started again): the clock takes up where the rig is now. It used to begin at bar 1 and
        // send every tick since at once. The follower is sent no START here: it joins at the next.
        if (! clkWas && ! starting) clkPpq = ppq + lookBeats;
        // Where the clock should be by the end of this block, and how far it goes toward it: at
        // most twice the tempo, so a jump of tempo (whose head start is then more beats) is made up
        // over a few blocks and not as two ticks at once.
        const double want = ppq + beats + lookBeats;
        const double c0 = clkPpq, c1 = run ? std::max (clkPpq, std::min (want, clkPpq + 2.0 * beats + 1.0e-9)) : clkPpq;
        // The ticks of [c0, c1) lie evenly across this block's own stretch of time, so no tick is
        // ever due before one already sent (at a steady tempo this is what it always was).
        const double span = c1 - c0;
        for (auto k = (juce::int64) std::ceil (c0 * 24.0 - 1e-9); span > 1.0e-12 && (double) k / 24.0 < c1 - 1e-12; ++k)
        {
            const double off = ((double) k / 24.0 - c0) / span * (double) n;
            pushClock (0xF8, base + off * 1000.0 / sr, samplesDone + (juce::int64) std::llround (off));
        }
        clkPpq = c1;
    }
    else clkPpq = 0.0;
    clkWas = clk;

    // Where a bar line falls in this buffer, noted against the tape: its sound leaves the master
    // chain that much later than it is made.
    if (starting || again) { ++runCount; barMoved = true; }
    // Has the rig held still? A change colours the sound for as long as the lanes and the master
    // hold it back; a knob still travelling is a change all the way.
    const juce::uint32 stampNow = stateStampV.load();
    if (stampNow != stampSeen) { stampSeen = stampNow; movedFor = maxLat.load() + masterLat.load() + n; }
    const bool movingNow = movedFor > 0 || glidingNow;
    if (movedFor > 0) movedFor -= n;
    if (movingNow) barMoved = true;
    if (run)
    {
        const double bar = std::ceil (ppq / 4.0 - 1e-9) * 4.0;
        if (bar < ppq + beats - 1e-12)
        {
            auto& m = marks[(size_t) (markCount.load() % kMarks)];
            m.sample = tapePos + (juce::int64) std::llround ((bar - ppq) * spb) + maxLat.load() + masterLat.load();
            m.run = runCount;
            // the bar that ends here: held if nothing moved in it, and counted from the second such in a row
            m.stamp = stampNow;
            m.steady = ! barMoved && ! settling;
            settling = barMoved;
            barMoved = movingNow;                   // (what moved in this buffer may be the new bar's)
            for (int l = 0; l < kLanes; ++l) m.sq[(size_t) l] = laneSq[(size_t) l];
            m.sq[(size_t) kLanes] = retSq[0]; m.sq[(size_t) kLanes + 1] = retSq[1];
            markCount.fetch_add (1);
        }
    }
    wasRunning = run;
    advanceControls (beats);
    gatherNotes (n, spb);

    const int keyIdx = (int) std::lround (cur[(size_t) cx::kKey] * 12.0f);
    sf::Bus::get().d().keyRoot.store (keyIdx - 1);
    const bool pan = panicNow.exchange (false);

    for (auto* b : { &mix, &sendA, &sendB }) b->clear (0, n);
    // who is heard: a soloed lane silences the other lanes; a soloed return silences every lane's dry signal
    bool anySolo = false;
    for (int l = 0; l < kLanes; ++l) anySolo = anySolo || cur[(size_t) cx::lane (l, cx::kSolo)] > 0.5f;
    const bool retSolo[2] = { cur[(size_t) cx::kRetMS + 1] > 0.5f, cur[(size_t) cx::kRetMS + 3] > 0.5f };
    const bool anyRetSolo = retSolo[0] || retSolo[1];
    const float dryNow = anyRetSolo ? 0.0f : 1.0f;
    const float fall = (float) (30.0 * n / sr);      // meters fall 30 dB a second
    juce::AudioBuffer<float> b2 (buf.getArrayOfWritePointers(), 2, n);
    for (int l = 0; l < kLanes; ++l)
    {
        auto& ln = lane[(size_t) l];
        if (! run && ln.patWait.load()) { ln.patWait = false; ln.patDirty = true; }      // the clock stopped: nothing to wait for
        if (ln.patDirty.load())
        {
            const juce::SpinLock::ScopedTryLockType g (patLock);
            if (g.isLocked()) { ln.player.pat = ln.shared; ln.patDirty = false; }
        }
        ln.midi.clear();
        if (stopping || pan || starting || again) ln.player.stop (ln.midi, 0);
        auto* voice = ln.voice.load();
        auto* voiceOut = ln.voiceOut.load();
        if (voice != ln.voiceSeen)
        {
            // a voice that has just taken the lane: every knob as it stands now (it was copied a moment before it was put here)
            ln.voiceSeen = voice;
            for (int k = 0; k < kLaneKnobs; ++k) applyBind (voice->bind[k], cur[(size_t) cx::lane (l, k)]);
        }
        if (pan)
        {
            voice->proc->panic();
            if (voiceOut != nullptr && ln.fading[kSlots]) voiceOut->proc->panic();
            for (auto& i : ln.ins) if (auto* in = i.load()) in->proc->panic();
            for (int k = 0; k < kSlots; ++k) if (auto* in = ln.out[k].load(); in != nullptr && ln.fading[k]) in->proc->panic();
        }
        // The lane's own sequencer muted: its steps keep their place and go unplayed, and what it
        // was holding ends now. Notes from outside still sound.
        const bool seqOff = cur[(size_t) cx::lane (l, cx::kSeqMute)] > 0.5f;
        if (seqOff && ! ln.seqOff) ln.player.stop (ln.midi, 0);
        ln.seqOff = seqOff;
        if (run)
        {
            // a recalled pattern is taken up on the bar line, to the sample: the block is rendered round it
            const double to = ppq + beats, bar = std::ceil (ppq / 4.0 - 1e-9) * 4.0;
            if (ln.patWait.load() && bar < to - 1e-12)
            {
                if (bar > ppq && ! seqOff) ln.player.render (ppq, bar, spb, n, ln.midi, ppq);
                {
                    const juce::SpinLock::ScopedTryLockType g (patLock);
                    if (g.isLocked()) ln.player.pat = ln.shared; else ln.patDirty = true;      // (the screen is writing it: next block)
                }
                ln.patWait = false;
                if (! seqOff) ln.player.render (std::max (ppq, bar), to, spb, n, ln.midi, ppq);
            }
            else if (! seqOff) ln.player.render (ppq, to, spb, n, ln.midi, ppq);
        }
        // notes from outside, on this lane's channel
        if (! liveMidi.isEmpty())
            for (const auto meta : liveMidi)
            {
                auto m = meta.getMessage();
                if (m.getChannel() != l + 1) continue;
                m.setChannel (1);
                ln.midi.addEvent (m, meta.samplePosition);
            }

        b2.clear();
        auto tp = clock();
        {
            // The instrument. While one is leaving, both are played the same notes and the lane
            // crosses from the old to the new.
            float f0 = 1.0f, f1 = 1.0f;
            const bool fade = fadeStep (ln, kSlots, beats, f0, f1);
            if (fade && voiceOut != nullptr)
            {
                juce::AudioBuffer<float> old (ghost.getArrayOfWritePointers(), 2, n);
                old.clear();
                ghostMidi.clear();
                ghostMidi.addEvents (ln.midi, 0, n, 0);
                voiceOut->proc->processBlock (old, ghostMidi);
                voice->proc->processBlock (b2, ln.midi);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* w = b2.getWritePointer (ch);
                    const float* o = old.getReadPointer (ch);
                    for (int i = 0; i < n; ++i) { const float g = f0 + (f1 - f0) * (float) i / (float) n; w[i] = o[i] + (w[i] - o[i]) * g; }
                }
            }
            else voice->proc->processBlock (b2, ln.midi);
        }
        profLane[(size_t) l] += since (tp);
        tp = clock();
        for (int sl = 0; sl < kSlots; ++sl)
        {
            auto* in = ln.ins[sl].load();
            auto* og = ln.out[sl].load();
            float f0 = 1.0f, f1 = 1.0f;
            const bool fade = fadeStep (ln, sl, beats, f0, f1);
            if (! fade) og = nullptr;                   // (its fade is done: it waits to be let go)
            // MUTE on an insert: the lane passes it by. The effect keeps running, so its tail is current when it comes back.
            const float g1 = cur[(size_t) cx::insMute (l, sl)] > 0.5f ? 0.0f : 1.0f, g0 = ln.slotG[sl];
            ln.slotG[sl] = g1;
            if (in == nullptr && ! fade) continue;
            if (in != nullptr)
            {
                // SOLO on an insert: only what the effect makes (its MIX held at full, its DRY switch off)
                const bool solo = cur[(size_t) cx::insSolo (l, sl)] > 0.5f;
                if (solo != in->soloOn)
                {
                    in->soloOn = solo;
                    if (solo && in->dryRaw != nullptr) in->dryWas = in->dryRaw->load();
                    if (in->mixCell >= 0) applyBind (in->bind[in->mixCell], solo ? 1.0f : cur[(size_t) cx::ins (l, sl, in->mixCell)]);
                    if (in->dryRaw != nullptr) in->dryRaw->store (solo ? 0.0f : in->dryWas);
                }
                if (! fade && g0 == 1.0f && g1 == 1.0f) { in->proc->processBlock (b2, noMidi); continue; }
            }
            juce::AudioBuffer<float> dry (pre.getArrayOfWritePointers(), 2, n);
            for (int ch = 0; ch < 2; ++ch) dry.copyFrom (ch, 0, b2, ch, 0, n);
            if (in != nullptr) in->proc->processBlock (b2, noMidi);
            if (fade)
            {
                // The effect that was here leaves as this one arrives: both are given the lane, and
                // the slot crosses from the old to the new. No effect on either side is the lane itself.
                const float* const* from = dry.getArrayOfReadPointers();
                juce::AudioBuffer<float> old (ghost.getArrayOfWritePointers(), 2, n);
                if (og != nullptr)
                {
                    for (int ch = 0; ch < 2; ++ch) old.copyFrom (ch, 0, dry, ch, 0, n);
                    og->proc->processBlock (old, noMidi);
                    from = old.getArrayOfReadPointers();
                }
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* w = b2.getWritePointer (ch);
                    const float* o = from[ch];
                    for (int i = 0; i < n; ++i) { const float g = f0 + (f1 - f0) * (float) i / (float) n; w[i] = o[i] + (w[i] - o[i]) * g; }
                }
            }
            if (g0 != 1.0f || g1 != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* w = b2.getWritePointer (ch);
                    const float* dr = dry.getReadPointer (ch);
                    for (int i = 0; i < n; ++i) { const float g = g0 + (g1 - g0) * (float) i / (float) n; w[i] = dr[i] + (w[i] - dr[i]) * g; }
                }
        }

        profIns[(size_t) l] += since (tp);
        // the lane EQ, after the inserts
        bool eqOn = false;
        for (int e = 0; e < 3; ++e)
        {
            const float want = eqDbFor (cur[(size_t) cx::lane (l, cx::kEqLow + e)]);
            ln.eqDb[e] += 0.25f * (want - ln.eqDb[e]);
            if (std::abs (want - ln.eqDb[e]) < 0.02f) ln.eqDb[e] = want;
            if (std::abs (ln.eqDb[e] - ln.eqSet[e]) > 0.01f) { setEq (ln, e, ln.eqDb[e]); ln.eqSet[e] = ln.eqDb[e]; }
            eqOn = eqOn || std::abs (ln.eqDb[e]) > 0.01f || std::abs (want) > 0.01f;
        }
        const int dly = ln.delay.load();
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = b2.getWritePointer (ch);
            if (eqOn) for (int i = 0; i < n; ++i) x[i] = ln.eq[2].tick (ln.eq[1].tick (ln.eq[0].tick (x[i], ch), ch), ch);
            // delay compensation: every lane leaves as late as the latest one
            auto& d = ln.dl[ch];
            int pos = ln.dlPos;
            for (int i = 0; i < n; ++i)
            {
                d[(size_t) pos] = x[i];
                x[i] = d[(size_t) ((pos - dly + 8192) & 8191)];
                pos = (pos + 1) & 8191;
            }
        }
        ln.dlPos = (ln.dlPos + n) & 8191;

        // MUTE and SOLO: a silenced lane still runs, so it comes back in time
        const int so = solo.load();
        const bool heard = cur[(size_t) cx::lane (l, cx::kMute)] < 0.5f && (! anySolo || cur[(size_t) cx::lane (l, cx::kSolo)] > 0.5f) && (so < 0 || so == l);
        const float lg = heard ? 1.0f : 0.0f;
        if (so >= 0 && so != l) ln.gainWas = 0.0f;       // a test's solo is in force from the first sample
        if (lg != 1.0f || ln.gainWas != 1.0f) for (int ch = 0; ch < 2; ++ch) b2.applyGainRamp (ch, 0, n, ln.gainWas, lg);
        ln.gainWas = lg;

        const float lp = b2.getMagnitude (0, n);
        ln.hold = std::max (juce::Decibels::gainToDecibels (lp, -100.0f), ln.hold - fall);
        laneDb[(size_t) l] = ln.hold;
        laneMax[(size_t) l] = std::max (laneMax[(size_t) l], lp);
        for (int ch = 0; ch < 2; ++ch) { const float* x = b2.getReadPointer (ch); double q = 0.0; for (int i = 0; i < n; ++i) q += (double) x[i] * x[i]; laneSq[(size_t) l] += q; }

        if (rec != nullptr)
        {
            // the lane as it goes into the mix (a soloed return silences it there, and so here)
            if (dryMixWas == 1.0f && dryNow == 1.0f) rec->track (1 + l, b2.getReadPointer (0), b2.getReadPointer (1), n);
            else
            {
                for (int ch = 0; ch < 2; ++ch) recBuf.copyFromWithRamp (ch, 0, b2.getReadPointer (ch), n, dryMixWas, dryNow);
                rec->track (1 + l, recBuf.getReadPointer (0), recBuf.getReadPointer (1), n);
            }
        }
        const float sa = cur[(size_t) cx::lane (l, cx::kSendA)], sb = cur[(size_t) cx::lane (l, cx::kSendB)];
        const float ga = sa * sa, gb = sb * sb;
        for (int ch = 0; ch < 2; ++ch)
        {
            mix.addFromWithRamp (ch, 0, b2.getReadPointer (ch), n, dryMixWas, dryNow);
            sendA.addFromWithRamp (ch, 0, b2.getReadPointer (ch), n, ln.sendWas[0], ga);
            sendB.addFromWithRamp (ch, 0, b2.getReadPointer (ch), n, ln.sendWas[1], gb);
        }
        ln.sendWas[0] = ga; ln.sendWas[1] = gb;
    }
    dryMixWas = dryNow;

    juce::AudioBuffer<float> a2 (sendA.getArrayOfWritePointers(), 2, n), bb2 (sendB.getArrayOfWritePointers(), 2, n), m2 (mix.getArrayOfWritePointers(), 2, n);
    // SPACE changing reverbs: the one leaving fades over this block; the one arriving is emptied of
    // whatever it held when it last ran, and comes up once that is done.
    const int spaceNext = spaceWant.load();
    const bool spaceSwap = spaceNext != spaceNow;
    if (pan) { retA->panic(); space[(size_t) spaceNow]->panic(); }
    auto tr = clock();
    retA->processBlock (a2, noMidi);
    space[(size_t) spaceNow]->processBlock (bb2, noMidi);
    profRet += since (tr);
    const bool spaceQuiet = spaceSwap || spaceHold > 0;
    spaceHold = std::max (0, spaceHold - n);
    if (spaceSwap) { spaceNow = spaceNext; space[(size_t) spaceNow]->panic(); spaceHold = (int) (sr * 0.02); }
    const float rg[2] = { cur[(size_t) cx::kRetMS] < 0.5f && (! anyRetSolo || retSolo[0]) ? 1.0f : 0.0f,
                          cur[(size_t) cx::kRetMS + 2] < 0.5f && (! anyRetSolo || retSolo[1]) && ! spaceQuiet ? 1.0f : 0.0f };
    for (int ch = 0; ch < 2; ++ch)
    {
        m2.addFromWithRamp (ch, 0, a2.getReadPointer (ch), n, retGainWas[0], rg[0]);
        m2.addFromWithRamp (ch, 0, bb2.getReadPointer (ch), n, retGainWas[1], rg[1]);
    }
    for (int k = 0; k < 2; ++k)
    {
        const auto& wet = k == 0 ? a2 : bb2;
        double q = 0.0;
        for (int ch = 0; ch < 2; ++ch) { const float* x = wet.getReadPointer (ch); for (int i = 0; i < n; ++i) q += (double) x[i] * x[i]; }
        retSq[(size_t) k] += q * (double) (rg[k] * rg[k]);
    }
    if (rec != nullptr)
        for (int k = 0; k < 2; ++k)
        {
            // each return as it goes into the mix
            const auto& wet = k == 0 ? a2 : bb2;
            for (int ch = 0; ch < 2; ++ch) recBuf.copyFromWithRamp (ch, 0, wet.getReadPointer (ch), n, retGainWas[k], rg[k]);
            rec->track (1 + kLanes + k, recBuf.getReadPointer (0), recBuf.getReadPointer (1), n);
        }
    retGainWas[0] = rg[0]; retGainWas[1] = rg[1];

    float inPk = 0.0f;
    tr = clock();
    for (size_t k = 0; k < master.size(); ++k)
    {
        if (k + 1 == master.size()) inPk = m2.getMagnitude (0, n);
        master[k]->processBlock (m2, noMidi);
    }
    profMaster += since (tr);

    // meters
    bool flt = false;
    for (auto& ln : lane) flt = flt || ln.voice.load()->proc->fault.load();
    for (auto& m : master) flt = flt || m->fault.load();
    fault = flt;
    const float* L = m2.getReadPointer (0); const float* R = m2.getReadPointer (1);
    float pk[2] = { 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        if (! (std::isfinite (l) && std::isfinite (r))) { l = r = 0.0f; }
        pk[0] = std::max (pk[0], std::abs (l)); pk[1] = std::max (pk[1], std::abs (r));
        loud->m.push (l, r);
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = juce::Decibels::gainToDecibels (pk[ch], -100.0f);
        peakHold[ch] = std::max (db, peakHold[ch] - fall);
        if (pk[ch] >= 0.999f) (ch == 0 ? clipL : clipR) = true;
    }
    peakL = peakHold[0]; peakR = peakHold[1];
    lufs = loud->m.shortTerm;
    const float outPk = std::max (pk[0], pk[1]);
    const float gr = inPk > 1e-4f && outPk > 1e-5f ? juce::jlimit (-24.0f, 0.0f, juce::Decibels::gainToDecibels (outPk / inPk)) : 0.0f;
    gainRed = gainRed.load() + 0.2f * (gr - gainRed.load());

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* s = m2.getReadPointer (ch);
        float* d = out.getWritePointer (ch);
        for (int i = 0; i < n; ++i) d[i] = std::isfinite (s[i]) ? juce::jlimit (-1.0f, 1.0f, s[i]) : 0.0f;
    }
    if (rec != nullptr) { rec->track (0, out.getReadPointer (0), out.getReadPointer (1), n); rec->end (n); }
    renderCue (n, run, ppq, beats);
    // the tape
    {
        const int at = (int) (tapePos & (kTape - 1)), first = std::min (n, kTape - at);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::FloatVectorOperations::copy (tape[(size_t) ch].data() + at, out.getReadPointer (ch), first);
            if (first < n) juce::FloatVectorOperations::copy (tape[(size_t) ch].data(), out.getReadPointer (ch) + first, n - first);
        }
        tapePos += n;
        tapeEnd = tapePos;
    }
    if (run) ppq += beats;
    profAll += since (tAll);
    ppqShown = ppq;
    samplesDone += n;
    blockCount.fetch_add (1);
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    prepare (d->getCurrentSampleRate(), std::max (64, d->getCurrentBufferSizeSamples()));
    // (on the Mac the device's own figure already holds one buffer)
    devBufMs = (double) d->getCurrentBufferSizeSamples() * 1000.0 / d->getCurrentSampleRate();
    outMs = (float) ((double) d->getOutputLatencyInSamples() * 1000.0 / d->getCurrentSampleRate());
}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outData, int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const auto t0 = juce::Time::getHighResolutionTicks();
    const double calledMs = juce::Time::getMillisecondCounterHiRes();
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (maxBlock, numSamples - done);
        juce::AudioBuffer<float> w (work.getArrayOfWritePointers(), 2, n);
        wallMs = calledMs + (double) done * 1000.0 / sr;
        render (w);
        for (int ch = 0; ch < numOut; ++ch)
            if (outData[ch] != nullptr)
            {
                if (ch < 2) juce::FloatVectorOperations::copy (outData[ch] + done, w.getReadPointer (ch), n);
                else if (ch < 4) juce::FloatVectorOperations::copy (outData[ch] + done, cueBuf.getReadPointer (ch - 2), n);     // the headphones
                else juce::FloatVectorOperations::clear (outData[ch] + done, n);
            }
        if (numOut >= 2 && outData[0] != nullptr && outData[1] != nullptr) phonesSplit (outData[0] + done, outData[1] + done, n);
        cueOuts = numOut >= 4 && outData[2] != nullptr && outData[3] != nullptr;
        done += n;
    }
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0), avail = numSamples / sr;
    cpu = cpu.load() + 0.1 * (used / avail - cpu.load());
}
} // namespace sf0
