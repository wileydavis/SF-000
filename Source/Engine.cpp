#include "Engine.h"
#include "core/Processor.h"
#include "core/Registry.h"
#include "core/Bus.h"
#include "core/Loudness.h"

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
Engine::Lane::Lane() { for (auto& i : ins) i = nullptr; }
Engine::Lane::~Lane() = default;

// ------------------------------------------------------------------------------------- building
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
        ln.proc = makeProc (d.module, d.fixed);
        ln.player.pitched = d.pitched;
        ln.shared = defaultPattern (l);
        const juce::String slug (d.slug);
        for (int k = 0; k < kLaneKnobs; ++k)
        {
            const int i = cx::lane (l, k);
            auto& c = controls[(size_t) i];
            bind[(size_t) i] = makeBind (*ln.proc, d.knobs[k], &c);
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
    for (auto& ln : lane) for (auto& i : ln.ins) delete i.exchange (nullptr);
}

int Engine::find (const juce::String& id) const
{
    for (int i = 0; i < cx::kCount; ++i) if (controls[(size_t) i].id == id) return i;
    return -1;
}

void Engine::set (int i, float v, double glideBeats)
{
    if (i < 0 || i >= cx::kCount) return;
    targetV[(size_t) i] = juce::jlimit (0.0f, 1.0f, v);
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
void Engine::setInsertType (int l, int slot, int type, bool defaults, const SetupState* setup)
{
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
            else applyBind (inst->bind[c], target (i));
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
    if (auto* old = ln.ins[slot].exchange (inst)) retired.push_back ({ old, blockCount.load() });
    collectRetired (false);
    updateLatency();
}

void Engine::collectRetired (bool all)
{
    // An insert taken out is let go once the audio thread has gone two blocks without it: deleted,
    // or kept aside if it holds files.
    const auto now = blockCount.load();
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
        lat[l] = lane[(size_t) l].proc->getLatencySamples();
        for (auto& i : lane[(size_t) l].ins) if (auto* in = i.load()) lat[l] += in->proc->getLatencySamples();
        mx = std::max (mx, lat[l]);
    }
    mx = std::min (mx, 8000);
    for (int l = 0; l < kLanes; ++l) lane[(size_t) l].delay = juce::jlimit (0, 8000, mx - lat[l]);
    maxLat = mx;
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
    spaceWant = model;
    if (! prepared) spaceNow = model;
}

void Engine::panic() { panicNow = true; }

// ---------------------------------------------------------------------------------------- setup
SFProcessor* Engine::processorFor (Target t) const
{
    if (t.kind == 0) return lane[(size_t) t.lane].proc.get();
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
    sr = rate; maxBlock = std::max (16, block);
    auto prep = [this] (SFProcessor& p) { p.setRateAndBufferSizeDetails (sr, maxBlock); p.prepareToPlay (sr, maxBlock); };
    for (auto& ln : lane)
    {
        prep (*ln.proc);
        for (auto& i : ln.ins) if (auto* in = i.load()) prep (*in->proc);
        ln.midi.ensureSize (4096);
        ln.patWait = false;
        for (auto& d : ln.dl) d.assign (8192, 0.0f);
        ln.dlPos = 0;
        for (auto& e : ln.eq) e = Biquad();
        for (auto& s : ln.eqSet) s = 99.0f;
        ln.patDirty = true;
    }
    for (auto* pk : parked) prep (*pk->proc);
    prep (*retA);
    for (auto& sp : space) prep (*sp);
    spaceNow = spaceWant.load(); spaceHold = 0;
    for (auto& m : master) prep (*m);
    for (auto* b : { &buf, &mix, &sendA, &sendB, &work, &pre }) b->setSize (2, maxBlock, false, true, false);
    liveMidi.ensureSize (8192);
    wallValid = false; wallMs = -1.0; noteKeptN = 0; clkPpq = 0.0;
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
                if (k >= cx::kIns && k < cx::kGen)
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
    const bool run = running.load();
    blockBpm = tempoFor (cur[(size_t) cx::kTempo]);
    const double beats = (double) n * blockBpm / 60.0 / sr, spb = sr * 60.0 / blockBpm;
    const bool starting = run && ! wasRunning, stopping = ! run && wasRunning;

    // When this block is on the wall clock, smoothed, so MIDI in and the clock out keep an even
    // pace however unevenly the device calls. (A test has no device: its clock is the samples.)
    const double durMs = (double) n * 1000.0 / sr, nowMs = wallMs >= 0.0 ? wallMs : virtualMs;
    wallMs = -1.0; virtualMs += durMs;
    if (! wallValid || std::abs (nowMs - (blockMs + lastDurMs)) > 30.0) { blockMs = nowMs; prevBlockMs = nowMs - durMs; wallValid = true; }
    else { prevBlockMs = blockMs; blockMs += lastDurMs + 0.1 * (nowMs - (blockMs + lastDurMs)); }
    lastDurMs = durMs;

    // MIDI clock out. A follower's note for a beat has to be back here a block before that beat
    // is rendered, so the clock runs ahead of the sound: two blocks (one for the sender to be on
    // time, one for the note's way in) and LEAD, the follower's own delay. RUN waits that long.
    // Ticks go out only while it runs, as Live's did: the OXI One is set up for a clock that
    // starts with START, and falls back to its own when there is none.
    const bool clk = clockOn.load();
    const double lookBeats = clk ? (2.0 * durMs + (double) clockLead.load()) * blockBpm / 60000.0 : 0.0;
    if (starting) ppq = -lookBeats;
    if (clk)
    {
        const double base = blockMs + durMs;
        if (starting) { clkPpq = 0.0; pushClock (0xFA, base - 1.0, samplesDone); }       // START, a millisecond before its first tick
        if (stopping) pushClock (0xFC, base, samplesDone);
        const double c0 = clkPpq, c1 = run ? std::max (clkPpq, ppq + beats + lookBeats) : clkPpq;
        for (auto k = (juce::int64) std::ceil (c0 * 24.0 - 1e-9); (double) k / 24.0 < c1 - 1e-12; ++k)
        {
            const double off = ((double) k / 24.0 - c0) * spb;
            pushClock (0xF8, base + off * 1000.0 / sr, samplesDone + (juce::int64) std::llround (off));
        }
        clkPpq = c1;
    }
    else clkPpq = 0.0;

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
        if (stopping || pan || starting) ln.player.stop (ln.midi, 0);
        if (pan) { ln.proc->panic(); for (auto& i : ln.ins) if (auto* in = i.load()) in->proc->panic(); }
        if (run)
        {
            // a recalled pattern is taken up on the bar line, to the sample: the block is rendered round it
            const double to = ppq + beats, bar = std::ceil (ppq / 4.0 - 1e-9) * 4.0;
            if (ln.patWait.load() && bar < to - 1e-12)
            {
                if (bar > ppq) ln.player.render (ppq, bar, spb, n, ln.midi, ppq);
                {
                    const juce::SpinLock::ScopedTryLockType g (patLock);
                    if (g.isLocked()) ln.player.pat = ln.shared; else ln.patDirty = true;      // (the screen is writing it: next block)
                }
                ln.patWait = false;
                ln.player.render (std::max (ppq, bar), to, spb, n, ln.midi, ppq);
            }
            else ln.player.render (ppq, to, spb, n, ln.midi, ppq);
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
        ln.proc->processBlock (b2, ln.midi);
        profLane[(size_t) l] += since (tp);
        tp = clock();
        for (int sl = 0; sl < kSlots; ++sl)
            if (auto* in = ln.ins[sl].load())
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
                // MUTE on an insert: the lane passes it by. The effect keeps running, so its tail is current when it comes back.
                const float g1 = cur[(size_t) cx::insMute (l, sl)] > 0.5f ? 0.0f : 1.0f, g0 = in->gWas;
                in->gWas = g1;
                if (g0 == 1.0f && g1 == 1.0f) { in->proc->processBlock (b2, noMidi); continue; }
                juce::AudioBuffer<float> dry (pre.getArrayOfWritePointers(), 2, n);
                for (int ch = 0; ch < 2; ++ch) dry.copyFrom (ch, 0, b2, ch, 0, n);
                in->proc->processBlock (b2, noMidi);
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
    for (auto& ln : lane) flt = flt || ln.proc->fault.load();
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
    if (run) ppq += beats;
    profAll += since (tAll);
    ppqShown = ppq;
    samplesDone += n;
    blockCount.fetch_add (1);
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    prepare (d->getCurrentSampleRate(), std::max (64, d->getCurrentBufferSizeSamples()));
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
                else juce::FloatVectorOperations::clear (outData[ch] + done, n);
            }
        done += n;
    }
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0), avail = numSamples / sr;
    cpu = cpu.load() + 0.1 * (used / avail - cpu.load());
}
} // namespace sf0
