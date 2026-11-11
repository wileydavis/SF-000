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
        c.id = id; c.label = label; c.kind = k; c.def = def; c.snapshot = snap; c.morph = morph; c.used = true;
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
                ct.used = false; ct.snapshot = true; ct.morph = false;
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
    retB = makeProc (returnB().module, returnB().fixed);
    for (int k = 0; k < 4; ++k)
    {
        auto& a = controls[(size_t) (cx::kRet + k)];
        bind[(size_t) (cx::kRet + k)] = makeBind (*retA, returnA().cells[k], &a);
        a.id = juce::String ("ret.dub_") + returnA().cells[k].param; a.snapshot = true;
        auto& b = controls[(size_t) (cx::kRet + 4 + k)];
        bind[(size_t) (cx::kRet + 4 + k)] = makeBind (*retB, returnB().cells[k], &b);
        b.id = juce::String ("ret.space_") + returnB().cells[k].param; b.snapshot = true;
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
            c.id = juce::String ("mst.") + juce::String (st.knob.label).toLowerCase(); c.snapshot = false; c.morph = false;
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

void Engine::setPattern (int l, const Pattern& p)
{
    {
        const juce::SpinLock::ScopedLockType g (patLock);
        lane[(size_t) l].shared = p;
    }
    lane[(size_t) l].patDirty = true;
}

// -------------------------------------------------------------------------------------- inserts
void Engine::setInsertType (int l, int slot, int type, bool defaults)
{
    type = juce::jlimit (-1, (int) effects().size() - 1, type);
    auto& ln = lane[(size_t) l];
    InsertInst* inst = nullptr;
    if (type >= 0)
    {
        const auto& d = effects()[(size_t) type];
        inst = new InsertInst();
        inst->type = type;
        inst->proc = makeProc (d.module, d.fixed);
        for (int c = 0; c < kCells; ++c)
        {
            const int i = cx::ins (l, slot, c);
            auto& ct = controls[(size_t) i];
            const juce::String id = ct.id;
            if (d.cells[c].label != nullptr) { inst->bind[c] = makeBind (*inst->proc, d.cells[c], &ct); ct.morph = false; }
            else { ct.used = false; ct.label = {}; ct.choices.clear(); ct.kind = Kind::Knob; ct.def = 0.0f; }
            ct.id = id; ct.snapshot = true;
            if (defaults) set (i, ct.def);
            else applyBind (inst->bind[c], target (i));
            if (d.cells[c].label != nullptr && juce::String (d.cells[c].param) == "mix") inst->mixCell = c;
        }
        if (inst->proc->paramIndex ("dry") >= 0) inst->dryRaw = inst->proc->apvts.getRawParameterValue ("dry");
        if (prepared) { inst->proc->setRateAndBufferSizeDetails (sr, maxBlock); inst->proc->prepareToPlay (sr, maxBlock); }
    }
    else
        for (int c = 0; c < kCells; ++c)
        {
            auto& ct = controls[(size_t) cx::ins (l, slot, c)];
            ct.used = false; ct.label = {}; ct.choices.clear(); ct.kind = Kind::Knob;
        }
    insType[(size_t) l][(size_t) slot] = type;
    if (auto* old = ln.ins[slot].exchange (inst)) retired.push_back ({ old, blockCount.load() });
    collectRetired (false);
    updateLatency();
}

void Engine::collectRetired (bool all)
{
    // An insert taken out is deleted once the audio thread has gone two blocks without it.
    const auto now = blockCount.load();
    for (size_t i = retired.size(); i-- > 0;)
        if (all || now >= retired[i].second + 2 || ! prepared) { delete retired[i].first; retired.erase (retired.begin() + (long) i); }
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

bool Engine::loadFile (int l, const juce::File& f)
{
    auto& m = lane[(size_t) l].proc->mod();
    if (! m.acceptsFiles()) return false;
    {
        // unreadable (macOS has not been allowed into that folder yet, or the file is gone): say so
        juce::FileInputStream in (f);
        if (! in.openedOk()) return false;
    }
    m.loadFiles (juce::StringArray (f.getFullPathName()));
    files[(size_t) l] = f.getFullPathName();
    return true;
}

void Engine::panic() { panicNow = true; }

// ---------------------------------------------------------------------------------------- setup
SFProcessor* Engine::processorFor (Target t) const
{
    if (t.kind == 0) return lane[(size_t) t.lane].proc.get();
    if (t.kind == 1) { auto* in = lane[(size_t) t.lane].ins[t.slot].load(); return in != nullptr ? in->proc.get() : nullptr; }
    return t.slot == 0 ? retA.get() : retB.get();
}

std::vector<std::string> Engine::boundIds (Target t) const
{
    std::vector<std::string> ids;
    auto add = [&ids] (const KnobDef* k, int n) { for (int i = 0; i < n; ++i) if (k[i].param != nullptr) ids.push_back (k[i].param); };
    if (t.kind == 0) add (lanes()[(size_t) t.lane].knobs, kLaneKnobs);
    else if (t.kind == 1) { const int ty = insType[(size_t) t.lane][(size_t) t.slot]; if (ty >= 0) add (effects()[(size_t) ty].cells, kCells); }
    else add ((t.slot == 0 ? returnA() : returnB()).cells, kCells);
    return ids;
}

std::vector<Engine::SetupParam> Engine::setupParams (Target t) const
{
    std::vector<SetupParam> out;
    auto* p = processorFor (t);
    if (p == nullptr) return out;
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
        const auto bound = boundIds (t);
        const auto& specs = p->allSpecs();
        for (int i = 0; i < p->numModuleParams(); ++i)
            if (std::find (bound.begin(), bound.end(), specs[(size_t) i].id) == bound.end())
                st.push_back ({ specs[(size_t) i].id, p->param (i)->getValue() });
    }
    return st;
}

void Engine::applySetup (Target t, const SetupState& st)
{
    if (auto* p = processorFor (t))
    {
        const auto bound = boundIds (t);
        for (auto& kv : st)
        {
            const int i = p->paramIndex (kv.first.toStdString());
            if (i < 0 || i >= p->numModuleParams() || std::find (bound.begin(), bound.end(), kv.first.toStdString()) != bound.end()) continue;
            auto* prm = p->param (i);
            if (std::abs (prm->getValue() - kv.second) > 1e-6f) prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, kv.second));
        }
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
        for (auto& d : ln.dl) d.assign (8192, 0.0f);
        ln.dlPos = 0;
        for (auto& e : ln.eq) e = Biquad();
        for (auto& s : ln.eqSet) s = 99.0f;
        ln.patDirty = true;
    }
    prep (*retA); prep (*retB);
    for (auto& m : master) prep (*m);
    for (auto* b : { &buf, &mix, &sendA, &sendB, &work, &pre }) b->setSize (2, maxBlock, false, true, false);
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
            if (len > 0.0f && controls[s].morph)
            {
                // a glide starts on the next beat while the clock runs
                gStart[s] = wasRunning ? gClock - beats + (std::ceil (ppq - 1e-9) - ppq) : gClock - beats;
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
    const bool run = running.load();
    blockBpm = tempoFor (cur[(size_t) cx::kTempo]);
    const double beats = (double) n * blockBpm / 60.0 / sr, spb = sr * 60.0 / blockBpm;
    const bool starting = run && ! wasRunning, stopping = ! run && wasRunning;
    if (starting) ppq = 0.0;
    wasRunning = run;
    advanceControls (beats);

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
        if (ln.patDirty.load())
        {
            const juce::SpinLock::ScopedTryLockType g (patLock);
            if (g.isLocked()) { ln.player.pat = ln.shared; ln.patDirty = false; }
        }
        ln.midi.clear();
        if (stopping || pan || starting) ln.player.stop (ln.midi, 0);
        if (pan) { ln.proc->panic(); for (auto& i : ln.ins) if (auto* in = i.load()) in->proc->panic(); }
        if (run) ln.player.render (ppq, ppq + beats, spb, n, ln.midi);

        b2.clear();
        ln.proc->processBlock (b2, ln.midi);
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
    if (pan) { retA->panic(); retB->panic(); }
    retA->processBlock (a2, noMidi);
    retB->processBlock (bb2, noMidi);
    const float rg[2] = { cur[(size_t) cx::kRetMS] < 0.5f && (! anyRetSolo || retSolo[0]) ? 1.0f : 0.0f,
                          cur[(size_t) cx::kRetMS + 2] < 0.5f && (! anyRetSolo || retSolo[1]) ? 1.0f : 0.0f };
    for (int ch = 0; ch < 2; ++ch)
    {
        m2.addFromWithRamp (ch, 0, a2.getReadPointer (ch), n, retGainWas[0], rg[0]);
        m2.addFromWithRamp (ch, 0, bb2.getReadPointer (ch), n, retGainWas[1], rg[1]);
    }
    retGainWas[0] = rg[0]; retGainWas[1] = rg[1];

    float inPk = 0.0f;
    for (size_t k = 0; k < master.size(); ++k)
    {
        if (k + 1 == master.size()) inPk = m2.getMagnitude (0, n);
        master[k]->processBlock (m2, noMidi);
    }

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
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (maxBlock, numSamples - done);
        juce::AudioBuffer<float> w (work.getArrayOfWritePointers(), 2, n);
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
