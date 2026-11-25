#include "Engine.h"
#include <complex>
#include "core/Processor.h"
#include "core/Registry.h"
#include "core/Bus.h"
#include "core/Loudness.h"
#include "core/Pocket.h"
#include "core/Motion.h"
#include "modules/SynthEngine.h"        // (SF-000, the hundred and second build: its patches)
#include <map>
#include <mutex>

namespace sf0
{
static const double kGlideBars[] = { 0, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256 };
static const char* const kGlideNames[] = { "OFF", "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS", "64 BARS", "128 BARS", "256 BARS" };
// MOTION on the snapshot knobs: the suite's waves, less the two that listen to a sound of the
// module's own. (Its numbers for them, in this order.)
static const char* const kWaveNames[] = { "OFF", "SINE", "TRIANGLE", "SQUARE", "RAMP UP", "RAMP DOWN", "RANDOM", "DRIFT", "STEPS", "ENVELOPE", "KICK" };
static const int kWaveSrc[] = { sf::MOT_OFF, sf::MOT_SINE, sf::MOT_TRI, sf::MOT_SQUARE, sf::MOT_RAMP_UP, sf::MOT_RAMP_DOWN, sf::MOT_RANDOM, sf::MOT_DRIFT, sf::MOT_STEPS, sf::MOT_ENV, sf::MOT_KICK };
constexpr int kWaves = 11;
struct Engine::MotState { std::array<sf::Motion, cx::kMotUnits> wave; };
juce::StringArray Engine::motionWaves() { juce::StringArray a; for (auto* n : kWaveNames) a.add (n); return a; }
juce::StringArray Engine::motionSpeeds() { juce::StringArray a; for (auto& n : sf::motionRateNames()) a.add (juce::String (n)); return a; }
static const char* const kKeyNames[] = { "OFF", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

double glideBarsFor (float v)   { return kGlideBars[juce::jlimit (0, 11, (int) std::lround (v * 11.0f))]; }
double tempoFor (float v)       { return 60.0 + 140.0 * (double) juce::jlimit (0.0f, 1.0f, v); }
float tempoToValue (double bpm) { return (float) juce::jlimit (0.0, 1.0, (bpm - 60.0) / 140.0); }

// Where each lane sits in the two returns to begin with (SEND A the dub delay, SEND B the reverb).
static const float kSendDefaults[kLanes][2] = {
    { 0.0f, 0.0f }, { 0.12f, 0.0f }, { 0.2f, 0.2f }, { 0.0f, 0.35f }, { 0.3f, 0.2f }, { 0.0f, 0.0f },
    { 0.0f, 0.0f }, { 0.35f, 0.15f }, { 0.45f, 0.3f }, { 0.0f, 0.4f }, { 0.3f, 0.3f } };

static const char* const kModelId = "__model";       // SPACE's reverb, among its setup

// (and with the master's loudness: each lane's, the pocket each lane may have after its effects, and one run on nothing
// but a level of 1, to see the shape of the watched lane's)
struct Engine::Loud { sf::LoudnessMeter m; sf::LoudnessMeter lane[kLanes]; sf::Pocket post[kLanes]; sf::Pocket probe; };
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
    if (offline) p->setNonRealtime (true);
    return p;
}

void Engine::setOffline (bool on)
{
    offline = on;
    auto set = [on] (SFProcessor* p) { if (p != nullptr) p->setNonRealtime (on); };
    for (auto& ln : lane)
    {
        if (auto* v = ln.voice.load()) set (v->proc.get());
        if (auto* v = ln.voiceOut.load()) set (v->proc.get());
        for (auto& i : ln.ins) if (auto* in = i.load()) set (in->proc.get());
        for (auto& i : ln.out) if (auto* in = i.load()) set (in->proc.get());
    }
    for (auto* in : parked) set (in->proc.get());
    set (retA.get()); set (busProc.get());
    for (auto& sp : space) set (sp.get());
    for (auto& m : master) set (m.get());
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
    b.pi = i;
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

float Engine::startOf (const Bind& b)
{
    if (b.raw == nullptr) return 0.0f;
    const float plain = b.param->convertFrom0to1 (b.param->getDefaultValue());
    if (b.n > 0) return juce::jlimit (0.0f, 1.0f, plain / (float) std::max (1, b.n - 1));
    return juce::jlimit (0.0f, 1.0f, (b.param->getDefaultValue() - b.lo) / std::max (1e-6f, b.hi - b.lo));
}

// ---- a model's own sound knobs
int Engine::mapOf (int l, SFProcessor& p) const
{
    const auto& own = lanes()[(size_t) l].own;
    if (own.empty()) return 0;
    const int mi = p.paramIndex ("model");
    if (mi < 0) return 0;
    const juce::String model = p.param (mi)->getCurrentValueAsText();
    for (size_t m = 0; m < own.size(); ++m) if (model.equalsIgnoreCase (own[m].model)) return (int) m + 1;
    return 0;
}

unsigned int Engine::lanePlate (int l) const
{
    const auto& d = lanes()[(size_t) l];
    const auto* v = lane[(size_t) l].voice.load();
    const int m = v != nullptr ? v->map.load() : 0;
    return m > 0 && m <= (int) d.own.size() ? d.own[(size_t) m - 1].plate : 0u;
}

const KnobDef& Engine::laneKnob (int l, int k) const
{
    const auto& d = lanes()[(size_t) l];
    const auto* v = lane[(size_t) l].voice.load();
    const int m = v != nullptr ? v->map.load() : 0;
    return m > 0 && k < kSoundKnobs ? d.own[(size_t) m - 1].knobs[k] : d.knobs[k];
}

void Engine::bindVoice (int l, Voice& v)
{
    const auto& d = lanes()[(size_t) l];
    v.maps.assign (1 + d.own.size(), {});
    for (size_t m = 0; m < v.maps.size(); ++m)
        for (int k = 0; k < kLaneKnobs; ++k)
            v.maps[m][(size_t) k] = makeBind (*v.proc, m > 0 && k < kSoundKnobs ? d.own[m - 1].knobs[k] : d.knobs[k], nullptr);
    v.map.store (mapOf (l, *v.proc));
    // its pocket, if it has one: the switch that leaves it to be made after the lane's effects, its depth, its shape
    auto raw = [&v] (const char* id) -> std::atomic<float>* { return id != nullptr && v.proc->paramIndex (id) >= 0 ? v.proc->apvts.getRawParameterValue (id) : nullptr; };
    v.plays = raw ("plays");
    // (SF-000, the hundred and thirteenth build) Wiley, Nov 23: "I set the fileter envelop using the graphical curve in the
    // display, and maybe because that has a cutof knob that doesn't move as you move the curve, it didn't get saved with
    // the snapshot?" and "the knobs and the graphicl curves don't move with eachother but they probably should." A display
    // set the parameter under the knob, the knob stood where it was, and a snapshot kept the knob: what a display moves
    // that a panel knob reaches now turns the knob, and the knob sets it.
    {
        auto& mod = v.proc->mod();
        auto own = mod.setParam;
        Voice* vp = &v;
        mod.setParam = [this, l, vp, own] (int i, float plain)
        {
            if (lane[(size_t) l].voice.load() == vp)
            {
                const int now = vp->map.load();
                if (now >= 0 && now < (int) vp->maps.size())
                    for (int k = 0; k < kLaneKnobs; ++k)
                        if (const auto& b = vp->maps[(size_t) now][(size_t) k]; b.pi == i && b.n == 0 && b.param != nullptr)
                        {
                            const float place = juce::jlimit (0.0f, 1.0f, (b.param->convertTo0to1 (plain) - b.lo) / std::max (1e-6f, b.hi - b.lo));
                            if (handSet) handSet (cx::lane (l, k), place); else set (cx::lane (l, k), place);
                            return;
                        }
            }
            if (own) own (i, plain);
        };
    }
    v.pkPost = raw ("pocket_post"); v.pkDepth = v.pkPost != nullptr ? raw (pocketDepthId (l)) : nullptr; v.pkShape = raw ("pocket_shape");
    // (SF-000, the hundredth build) the lane's steps, for a display that shows what each plays (SAMPLE's)
    v.proc->mod().stepsOf = [this, l] (sf::Module::StepsView& sv)
    {
        const Pattern p = pattern (l);
        sv.count = juce::jlimit (0, sf::Module::StepsView::kMax, p.length);
        for (int k = 0; k < sv.count; ++k) { const auto& st = p.steps[(size_t) k]; sv.on[k] = st.on; sv.note[k] = st.note + 12 * (st.octave + 2); sv.vel[k] = st.vel; }
        sv.now = isRunning() && sv.count > 0 ? (int) (((juce::int64) std::floor (songPos() / kStepBeats)) % sv.count) : -1;
    };
}

const char* Engine::pocketDepthId (int l)
{
    const juce::String m (lanes()[(size_t) l].module);
    return m == "RUMBLE" || m == "BASS" || m == "LEAD" || m == "PAD" || m == "VOICE" ? "pocket_depth" : nullptr;      // (the lanes with a pocket: Lane.cpp's)
}

float Engine::pocketNow (int l, int which) const
{
    auto* v = lane[(size_t) l].voice.load();
    if (v == nullptr || v->pkDepth == nullptr) return 0.0f;
    auto* p = which == 0 ? v->pkDepth : which == 1 ? v->pkShape : v->pkPost;
    return p != nullptr ? p->load (std::memory_order_relaxed) : 0.0f;
}

void Engine::setHold (bool on, float db)
{
    auto* lim = masterStage ("LIMIT");
    if (lim == nullptr) return;
    const int ih = lim->paramIndex ("hold"), id = lim->paramIndex ("held");
    if (ih < 0 || id < 0) return;
    if (on) lim->param (id)->setValueNotifyingHost (lim->param (id)->convertTo0to1 (juce::jlimit (-12.0f, 12.0f, db > 999.0f ? trimWanted.load() : db)));
    lim->param (ih)->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

bool Engine::hold() const
{
    for (auto& m : master) if (m->getName() == "SF LIMIT") if (const int i = m->paramIndex ("hold"); i >= 0) return m->param (i)->getValue() > 0.5f;
    return false;
}

float Engine::heldDb() const
{
    for (auto& m : master) if (m->getName() == "SF LIMIT") if (const int i = m->paramIndex ("held"); i >= 0) return m->param (i)->convertFrom0to1 (m->param (i)->getValue());
    return 0.0f;
}

// The pocket, where a lane has it after its effects (the instrument then leaves it alone): the same one, made here from
// the instrument's own DEPTH and SHAPE. And, for the lane being watched, the shape of its pocket wherever it is made.
void Engine::postPocket (int l, juce::AudioBuffer<float>& b2, int n, bool)
{
    auto& ln = lane[(size_t) l];
    auto* v = ln.voice.load();
    const bool watched = lowWatch.load (std::memory_order_relaxed) == l;
    const bool has = v != nullptr && v->pkDepth != nullptr;
    if (! has && ! watched) return;
    const bool post = has && v->pkPost != nullptr && v->pkPost->load (std::memory_order_relaxed) > 0.5f;
    const float depth = has ? v->pkDepth->load (std::memory_order_relaxed) : 0.0f, shape = has && v->pkShape != nullptr ? v->pkShape->load (std::memory_order_relaxed) : 0.5f;
    sf::Ctx c;
    const double bps = blockBpm / 60.0 / sr;
    c.sr = sr; c.n = n; c.bpm = blockBpm; c.ppq = ppq - (double) ln.insLat * bps; c.playing = wasRunning; c.beatsPerBar = 4.0; c.bus = &sf::Bus::get(); c.samplePos = samplesDone;
    if (has && (post || ! loud->post[l].idle()))
    {
        float* ch[2] = { b2.getWritePointer (0), b2.getWritePointer (1) };
        loud->post[l].process (c, ch, 2, n, post ? depth : 0.0f, shape, nullptr, post && depth > 0.001f, false, false);
    }
    if (watched)
    {
        float g[2048];
        const int m = std::min (n, 2048);
        for (int i = 0; i < m; ++i) g[i] = 1.0f;
        if (has) { float* ch[1] = { g }; c.n = m; loud->probe.process (c, ch, 1, m, depth, shape, nullptr, depth > 0.001f, false, false); }
        const double t0 = ppq - (double) maxLat.load() * bps;
        for (int i = 0; i < m; ++i)
        {
            const double q = (t0 + (double) i * bps) * 0.25;
            const int bin = juce::jlimit (0, kLowBins - 1, (int) ((q - std::floor (q)) * (double) kLowBins));
            if (bin != lowPkBin) { if (lowPkBin >= 0) lowPk[(size_t) lowPkBin] = lowPkMin; lowPkBin = bin; lowPkMin = 1.0f; }
            lowPkMin = std::min (lowPkMin, g[i]);
        }
    }
}

void Engine::synthPatch (int l, SFProcessor& p, int patch)
{
    const auto& all = sf::syn::patches();
    if (patch < 0 || patch >= (int) all.size()) return;
    const auto& specs = p.allSpecs();
    std::vector<std::pair<int, float>> put;         // (each control's place, 0 to 1)
    for (int i = 0; i < p.numModuleParams(); ++i)
        if (juce::String (specs[(size_t) i].id).startsWith ("sy_")) put.push_back ({ i, p.param (i)->getDefaultValue() });
    auto place = [&] (int i, float v) { for (auto& x : put) if (x.first == i) { x.second = v; return; } put.push_back ({ i, v }); };
    for (auto& [pid, v] : all[(size_t) patch].values)
        if (const int i = p.paramIndex (pid); i >= 0 && i < p.numModuleParams()) place (i, juce::jlimit (0.0f, 1.0f, p.param (i)->convertTo0to1 (v)));
    if (const int i = p.paramIndex ("sy_patch"); i >= 0) place (i, p.param (i)->convertTo0to1 ((float) patch));
    auto* vc = lane[(size_t) l].voice.load();
    const int now = vc != nullptr ? vc->map.load() : 0;
    for (auto& [i, v] : put)
    {
        p.param (i)->setValueNotifyingHost (v);
        // (one of the panel's knobs reaches it: the knob stands there too, or it would put the parameter back)
        if (vc != nullptr && now > 0 && now < (int) vc->maps.size())
            for (int k = 0; k < kSoundKnobs; ++k)
                if (const auto& b = vc->maps[(size_t) now][(size_t) k]; b.pi == i && b.n == 0)
                    set (cx::lane (l, k), juce::jlimit (0.0f, 1.0f, (v - b.lo) / std::max (1e-6f, b.hi - b.lo)));
    }
}

bool Engine::remapLane (int l, bool fresh)
{
    auto& ln = lane[(size_t) l];
    auto* v = ln.voice.load();
    if (v == nullptr || v->maps.size() < 2) return false;
    const int was = v->map.load(), now = mapOf (l, *v->proc);
    if (was == now) return false;
    const auto& d = lanes()[(size_t) l];
    for (int k = 0; k < kSoundKnobs; ++k)
    {
        const int i = cx::lane (l, k);
        auto& ct = controls[(size_t) i];
        const bool same = v->maps[(size_t) was][(size_t) k].pi == v->maps[(size_t) now][(size_t) k].pi;
        const juce::String id = ct.id;
        makeBind (*v->proc, now > 0 ? d.own[(size_t) now - 1].knobs[k] : d.knobs[k], &ct);      // (its name, its kind, its menu)
        ct.id = id; ct.snapshot = true;
        ct.def = startOf (v->maps[(size_t) now][(size_t) k]);
        if (fresh && ! same) set (i, ct.def);
    }
    v->map.store (now);
    ln.knobsMoved.fetch_add (1);            // (the audio thread puts every knob to what it now reaches)
    stateMoved();
    return true;
}

void Engine::applyBind (const Bind& b, float v)
{
    if (b.raw == nullptr) return;
    if (b.n > 0) b.raw->store (std::round (v * (float) (b.n - 1)));
    else         b.raw->store (b.param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, b.lo + v * (b.hi - b.lo))));
}

Engine::Engine()
{
    SFProcessor::keyFromHost = true;        // KEY is this rig's own: the modules follow it with no SF CONTROL running
    for (auto& ln : lane)
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto d = eqDefault (b);
            ln.eqType[b] = d.type; ln.eqHz[b] = d.hz; ln.eqGain[b] = 0.0f; ln.eqQ[b] = d.q;
            ln.eqHzNow[b] = d.hz; ln.eqQNow[b] = d.q; ln.eqSet[b] = 99.0f; ln.eqTypeSet[b] = -1;
        }
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto d = eqDefault (b);
        auto& ln = masterEq;
        ln.eqType[b] = d.type; ln.eqHz[b] = d.hz; ln.eqGain[b] = 0.0f; ln.eqQ[b] = d.q;
        ln.eqHzNow[b] = d.hz; ln.eqQNow[b] = d.q; ln.eqSet[b] = 99.0f; ln.eqTypeSet[b] = -1;
    }
    for (int k = 0; k < kMixFaders; ++k) { mixPlace[(size_t) k] = kMixUnity; mixNow[k] = mixWas[k] = 1.0f; mixHold[k] = -100.0f; mixDb[(size_t) k] = -100.0f; }
    for (auto& a : idleDb) a = -100.0f;
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
        first->proc->setMotionSeed (motionSeedFor ({ 0, l, 0 }));
        ln.voice = first;
        ln.player.pitched = d.pitched;
        ln.shared = defaultPattern (l);
        const juce::String slug (d.slug);
        for (int k = 0; k < kLaneKnobs; ++k)
        {
            const int i = cx::lane (l, k);
            auto& c = controls[(size_t) i];
            makeBind (*first->proc, d.knobs[k], &c);          // (a lane's knobs reach its voice; the table's own bind stays empty)
            c.id = slug + "." + d.knobs[k].param;
            c.snapshot = true;
        }
        bindVoice (l, *first);
        first->map.store (0);
        remapLane (l, false);               // (a lane that starts on a model with knobs of its own)
        playsMoved (l);
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
        {
            // its pocket (POCKETS, in the master): a snapshot's, and they travel
            const auto& pd = pocketDefault (l);
            const float pv[kPkLane] = { Pockets::homeValue (pd.homeHz), Pockets::widthValue (pd.widthOct), pd.hold, pd.yield };
            static const char* const pn[kPkLane] = { "HOME", "WIDTH", "HOLD", "YIELD" };
            static const char* const pi[kPkLane] = { "pk_home", "pk_width", "pk_hold", "pk_yield" };
            for (int k = 0; k < kPkLane; ++k) named (cx::pk (l, k), slug + "." + pi[k], pn[k], Kind::Knob, pv[k], true, true);
        }
        static const char* const gn[] = { "HITS", "ROTATE", "ACCENTS", "SEED" };
        static const char* const gi[] = { "gen_hits", "gen_rotate", "gen_accents", "gen_seed" };
        static const float gd[] = { 0.31f, 0.5f, 0.2f, 0.0f };
        for (int k = 0; k < 4; ++k)
        {
            auto& c = named (cx::gen (l, k), slug + "." + gi[k], gn[k], k == 1 ? Kind::Bipolar : k == 3 ? Kind::List : Kind::Knob, gd[k], true, false);
            if (k == 3) for (int q = 1; q <= 128; ++q) c.choices.add (juce::String (q));     // (a place for each place of a controller's knob: Sequencer.h, seedAt)
        }
    }

    retA = makeProc (returnA().module, returnA().fixed);
    retA->setMotionSeed (motionSeedFor ({ 2, 0, 0 }));
    for (int m = 0; m < kSpaceModels; ++m) { space[(size_t) m] = makeProc (returnB (m).module, returnB (m).fixed); space[(size_t) m]->setMotionSeed (motionSeedFor ({ 2, 0, 1 })); }
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
            b.id = juce::String ("ret.") + spaceModelSlug (m) + "_" + returnB (m).cells[k].param; b.snapshot = true;
        }
    }
    // the drum bus: its four knobs are a snapshot's, travel on a recall, and a snapshot knob can reach them
    busProc = makeProc (drumBus().module, drumBus().fixed);
    busProc->setMotionSeed (motionSeedFor ({ 3, 0, 0 }));
    for (int k = 0; k < kBusKnobs; ++k)
    {
        auto& c = controls[(size_t) (cx::kBus + k)];
        bind[(size_t) (cx::kBus + k)] = makeBind (*busProc, drumBus().cells[k], &c);
        c.id = juce::String ("bus.") + drumBus().cells[k].param; c.snapshot = true;
    }
    for (int l = 0; l < kLanes; ++l) busIn[(size_t) l] = busProc->apvts.getRawParameterValue ("in_" + juce::String (l + 1));
    // POCKETS: with AMOUNT at nought, where it starts, nothing is carved
    named (cx::kPkG, "pk.amount", "AMOUNT", Kind::Knob, 0.0f, true, true);
    named (cx::kPkG + 1, "pk.clear", "CLEAR", Kind::Knob, 3.0f / 18.0f, true, true);
    named (cx::kPkG + 2, "pk.speed", "SPEED", Kind::Knob, 0.5f, true, true);
    named (cx::kPkG + 3, "pk.returns", "RETURNS", Kind::Knob, 0.5f, true, true);
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

    // GAIN: the whole mix into the master chain, 24 dB either way. Kept with the set, as TAPE, COMP and LIMIT are.
    named (cx::kGain, "mst.gain", "GAIN", Kind::Bipolar, 0.5f, false, false);
    // CLIP: how hard the limiter's own clipper takes the peaks before the limiter has to. Kept with the set too.
    if (auto* lim = masterStage ("LIMIT"))
    {
        auto& c = controls[(size_t) cx::kClip];
        bind[(size_t) cx::kClip] = makeBind (*lim, KnobDef { "CLIP", "clip", 'u' }, &c);
        c.id = "mst.clip"; c.snapshot = false; c.morph = false; c.glides = false;
    }
    named (cx::kTempo, "g.tempo", "TEMPO", Kind::Knob, tempoToValue (132.0), false, false);
    auto& key = named (cx::kKey, "g.key", "KEY", Kind::List, 0.0f, true, false);
    for (auto* n : kKeyNames) key.choices.add (n);
    auto& gl = named (cx::kGlide, "g.glide", "GLIDE", Kind::List, 0.0f, false, false);
    for (auto* n : kGlideNames) gl.choices.add (n);
    // MOTION on the snapshot knobs: off, a bar long, on the first knob, with no amount
    for (int u = 0; u < cx::kMotUnits; ++u)
    {
        const juce::String id = "mot." + juce::String (u + 1) + "_";
        named (cx::mot (u, 0), id + "wave", "WAVE", Kind::List, 0.0f, false, false).choices = motionWaves();
        named (cx::mot (u, 1), id + "speed", "SPEED", Kind::List, 4.0f / (float) (sf::kMotionRates - 1), false, false).choices = motionSpeeds();
        auto& kn = named (cx::mot (u, 2), id + "knob", "KNOB", Kind::List, 0.0f, false, false);
        for (int k = 1; k <= 16; ++k) kn.choices.add (juce::String (k));
        named (cx::mot (u, 3), id + "amount", "AMOUNT", Kind::Bipolar, 0.5f, false, false);
    }
    motState = std::make_unique<MotState>();
    for (int k = 0; k < 16; ++k)
        named (cx::kSnap + k, "snap.k" + juce::String (k + 1), {}, Kind::Knob, 0.0f, false, false);

    for (int i = 0; i < cx::kCount; ++i)
    {
        const float d = controls[(size_t) i].def;
        targetV[(size_t) i] = d; shownV[(size_t) i] = d; baseV[(size_t) i] = d; glideV[(size_t) i] = 0.0f; seqV[(size_t) i] = 0;
        cur[(size_t) i] = d; applied[(size_t) i] = d; snd[(size_t) i] = d; curSaid[(size_t) i] = d;
    }
    loud = std::make_unique<Loud>();
    for (auto& p : laneDb) p = -100.0f;
    for (auto& p : laneLufs) p = -70.0f;
    for (auto& row : lowBins) for (auto& b : row) b = 0.0f;
    for (auto& b : lowPk) b = 1.0f;
    for (int l = 0; l < kLanes; ++l) initSetup[(size_t) l] = getSetup ({ 0, l, 0 });

    // To begin with the lead has a delay and a radio on it, as the design shows.
    setInsertType (7, 0, effectIndex ("DELAY"));
    setInsertType (7, 1, effectIndex ("RADIO"));
    set (cx::ins (7, 1, 6), 0.35f);      // (its MIX: the seventh place, the hundred and sixth build)
}

Engine::~Engine()
{
    collectRetired (true);
    delete knobLaws.exchange (nullptr);
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
    if (! prepared) { shownV[(size_t) i] = targetV[(size_t) i].load(); baseV[(size_t) i] = targetV[(size_t) i].load(); }
}

void Engine::setKnobLaws (std::unique_ptr<KnobLaws> laws)
{
    // (the one the audio thread may still be reading is let go two blocks on: see collectRetired)
    if (auto* was = knobLaws.exchange (laws.release())) retiredLaws.push_back ({ was, blockCount.load() });
}

juce::String Engine::valueText (int i) const
{
    const auto& c = controls[(size_t) i];
    const float v = target (i);
    if (i == cx::kTempo) return juce::String (tempoFor (v), 1);
    if (i == cx::kGain) { const float db = gainDb (v); return (db > 0.05f ? "+" : "") + juce::String (std::abs (db) < 0.05f ? 0.0f : db, 1) + " DB"; }
    // MOTION's AMOUNT: how much of the knob's turn the wave swings it, and which way
    if (cx::isMot (i) && (i - cx::kMot) % cx::kMotKnobs == 3) { const int a = (int) std::lround ((v - 0.5f) * 200.0f); return a == 0 ? juce::String ("NONE") : (a > 0 ? "+" : "-") + juce::String (std::abs (a)); }
    if (cx::isPk (i))
    {
        const int k = i < cx::kRet ? (i % cx::kPerLane) - cx::kPk : 4 + (i - cx::kPkG);
        auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " KHZ" : juce::String ((int) std::lround (f)) + " HZ"; };
        switch (k)
        {
            case 0: return hz (Pockets::homeHz (v));
            case 1: return juce::String (Pockets::widthOct (v), 1) + " OCT";
            case 3: return juce::String ((int) std::lround (Pockets::kMaxCut * v)) + " DB";
            case 5: return juce::String ((int) std::lround (Pockets::clearDb (v))) + " DB";
            case 6: return juce::String ((int) std::lround (Pockets::releaseMs (v))) + " MS";
            case 7: return juce::String ((int) std::lround (Pockets::kMaxCut * v)) + " DB";
            default: break;         // HOLD and AMOUNT: a share
        }
    }
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

void Engine::setPattern (int l, const Pattern& p, bool onBarLine, double afterBeats)
{
    stateMoved();
    auto& ln = lane[(size_t) l];
    {
        const juce::SpinLock::ScopedLockType g (patLock);
        ln.shared = p;
    }
    if (onBarLine && prepared && running.load()) { ln.patAfter = (float) std::max (0.0, afterBeats); ln.patFresh = true; ln.patDirty = false; ln.patWait = true; }
    else { ln.patWait = false; ln.patDirty = true; }
}

// ----------------------------------------------------------------------------------------- MIDI
void Engine::noteIn (const juce::MidiMessage& m, double arrivedMs)
{
    if (! m.isNoteOnOrOff() || m.getChannel() < 1 || m.getChannel() > kLanes) return;
    if (m.isNoteOn()) idleNotesIn.fetch_add (1, std::memory_order_relaxed);      // (the idle watch: a note played in, not a sound of its own)
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
    if (m.isNoteOn()) idleNotesIn.fetch_add (1, std::memory_order_relaxed);      // (the idle watch: a note played in, not a sound of its own)
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
// The second rig's sound, for this buffer's own samples: the frames it wrote for them, if they are the wanted cue's
// and are all there. A cue changed or let go fades over 10 ms on the frames still coming; one whose frames are late
// is silence at once (the headphones only), and counted.
void Engine::renderCueRig (CueShared& f, int n)
{
    const int want = cueRigSerial.load();
    const juce::int64 at = samplesDone;
    const int serial = f.serial.load (std::memory_order_acquire);
    const juce::int64 from = f.validFrom.load (std::memory_order_acquire), written = f.written.load (std::memory_order_acquire);
    const bool there = written >= at + n && written - at <= (juce::int64) CueShared::kFrames;      // (frames for all of this buffer, and not written over since)
    if (cueRigGain <= 0.0f) cueRigPlaying = serial;
    // (what is fading is the cue its frames were: it goes on only while the second rig still writes that one)
    const bool frames = at >= from && there, same = serial == cueRigPlaying && frames;
    const float target = same && want != 0 && serial == want ? 1.0f : 0.0f;
    if (want != 0 && serial == want && ! frames && cueRigOn.load()) cueRigMissed.fetch_add (1);    // (the wanted one was playing and its frames are not here)
    cueRigOn = target > 0.0f;
    // the second rig renders while a cue is wanted or one is still going out
    f.want.store (want != 0 || cueRigGain > 0.0f ? 1 : 0, std::memory_order_release);
    if (! same) { cueRigGain = 0.0f; return; }
    if (target <= 0.0f && cueRigGain <= 0.0f) return;
    const float step = 1.0f / (float) (0.01 * sr);
    float* L = cueBuf.getWritePointer (0); float* R = cueBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        cueRigGain = target > cueRigGain ? std::min (target, cueRigGain + step) : std::max (target, cueRigGain - step);
        const size_t k = (size_t) ((at + i) & (juce::int64) (CueShared::kFrames - 1)) * 2;
        const float l = f.ring[k], r = f.ring[k + 1];
        L[i] = (std::isfinite (l) ? juce::jlimit (-1.0f, 1.0f, l) : 0.0f) * cueRigGain;
        R[i] = (std::isfinite (r) ? juce::jlimit (-1.0f, 1.0f, r) : 0.0f) * cueRigGain;
    }
}

void Engine::renderCue (int n, bool run, double ppq0, double beats)
{
    cueBuf.clear (0, n);
    if (auto* f = cueFeed.load()) renderCueRig (*f, n);
    else { cueRigGain = 0.0f; cueRigOn = false; }
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
        L[i] += (l[i0] + (l[i1] - l[i0]) * f) * cueGain;       // (onto the second rig's, if a clip is going out as that comes in)
        R[i] += (r[i0] + (r[i1] - r[i0]) * f) * cueGain;
    }
}

// PHONES: the set to the left ear and the cue to the right while a cue plays (or is fading away),
// coming in and going out over 10 ms.
void Engine::phonesSplit (float* left, float* right, int n)
{
    const float target = phonesOn.load() && (cueWant.load() != nullptr || cueNow != nullptr || cueRigSerial.load() != 0 || cueRigGain > 0.0f) ? 1.0f : 0.0f;
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
bool Engine::lastHeard (Heard& h, int wantBars, bool stems) const                          { return heard (h, wantBars, false, 0, stems); }
bool Engine::heldHeard (Heard& h, juce::uint32 stamp, int wantBars, bool stems) const      { return heard (h, wantBars, true, stamp, stems); }

void Engine::stemOut (RecSink* rec, int index, const float* l, const float* r, int n) noexcept
{
    if (rec != nullptr) rec->track (index, l, r, n);
    if (index < 1 || index > (int) stemTape.size()) return;
    auto& t = stemTape[(size_t) (index - 1)];
    const int at = (int) (tapePos & (kTape - 1)), first = std::min (n, kTape - at);
    juce::FloatVectorOperations::copy (t[0].data() + at, l, first);
    juce::FloatVectorOperations::copy (t[1].data() + at, r, first);
    if (first < n)
    {
        juce::FloatVectorOperations::copy (t[0].data(), l + first, n - first);
        juce::FloatVectorOperations::copy (t[1].data(), r + first, n - first);
    }
}


bool Engine::heard (Heard& h, int wantBars, bool heldOnly, juce::uint32 stamp, bool withStems) const
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
    if (withStems && bars > 0 && from - masterLat.load() >= 0 && end - (from - masterLat.load()) <= kTape - 65536)
    {
        // (a stem goes into the mix the master's delay before its sound leaves the master: its bars are that much earlier on its tape)
        const int sat = (int) ((from - masterLat.load()) & (kTape - 1)), sfirst = std::min (len, kTape - sat);
        h.stems.resize (stemTape.size());
        for (size_t k = 0; k < stemTape.size(); ++k)
        {
            h.stems[k].setSize (2, len);
            for (int ch = 0; ch < 2; ++ch)
            {
                h.stems[k].copyFrom (ch, 0, stemTape[k][(size_t) ch].data() + sat, sfirst);
                if (sfirst < len) h.stems[k].copyFrom (ch, sfirst, stemTape[k][(size_t) ch].data(), len - sfirst);
            }
        }
    }
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

void Engine::pushHeard (const HeardNote& h)
{
    const auto w = heardFifo.write (1);
    if (w.blockSize1 <= 0) { heardLost = true; return; }
    HeardNote e = h;
    if (heardLost) { e.kind = 3; heardLost = false; }           // (said in place of the first one there is room for again)
    heardQ[(size_t) w.startIndex1] = e;
}

int Engine::takeHeardNotes (HeardNote* into, int most)
{
    int got = 0;
    while (got < most)
    {
        const auto r = heardFifo.read (1);
        if (r.blockSize1 <= 0) break;
        into[got++] = heardQ[(size_t) r.startIndex1];
    }
    return got;
}

// The notes that came since the last block, each at the place in this block that matches when it
// came (the block before this one's span of time), so what was played evenly sounds evenly.
void Engine::gatherNotes (int n, double spb)
{
    liveMidi.clear();
    if (offline)
    {
        // a print: what is played in from outside meanwhile is not part of it
        noteKeptN = 0;
        for (;;) { const auto r = noteFifo.read (1); if (r.blockSize1 <= 0) break; }
        return;
    }
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
        // where on the song's time it was played, for a snapshot
        if (wasRunning) pushHeard ({ ppq + (double) at / spb, (juce::uint8) (e.b[0] & 0x0f), e.b[1], e.b[2], (juce::uint8) ((e.b[0] & 0xf0) == 0x90 && e.b[2] > 0 ? 1 : 0) });
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
            inst->proc->setMotionSeed (motionSeedFor ({ 1, l, slot }));
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

// A hold is over without more ado (a recall forks the lane, or the audio is starting again): the one that arrived stays.
void Engine::dropHold (int l)
{
    if (! heldLane[(size_t) l]) return;
    auto& ln = lane[(size_t) l];
    heldLane[(size_t) l] = false;
    ln.holdG.store (-1.0f); ln.holdEnd.store (0);
}

bool Engine::holdVoice (int l)
{
    if (heldLane[(size_t) l]) return true;
    if (! prepared) return false;
    auto& ln = lane[(size_t) l];
    heldFile[(size_t) l] = files[(size_t) l];
    ln.holdEnd.store (0); ln.holdBack.store (0);
    ln.holdG.store (0.0f);                  // (before the fork: the audio thread finds the hold with the new fade)
    forkVoice (l, 0.0);
    heldLane[(size_t) l] = true;
    return true;
}

void Engine::blendVoice (int l, float share)
{
    if (heldLane[(size_t) l] && lane[(size_t) l].holdEnd.load() == 0) lane[(size_t) l].holdG.store (juce::jlimit (0.0f, 1.0f, share));
}

void Engine::endHold (int l, bool onNew)
{
    if (! heldLane[(size_t) l]) return;
    lane[(size_t) l].holdEnd.store (onNew ? 1 : 2);
    if (! prepared) collectRetired (false);
}

void Engine::forkVoice (int l, double fadeBeats)
{
    dropHold (l);                           // (a lane held between two: the one that had arrived is the one playing, and leaves as any other)
    auto& ln = lane[(size_t) l];
    const auto& d = lanes()[(size_t) l];
    auto* was = ln.voice.load();
    auto* v = new Voice();
    v->proc = makeProc (d.module, d.fixed);
    v->proc->setMotionSeed (motionSeedFor ({ 0, l, 0 }));
    // as the one playing stands, to the last parameter (CONFIG's too), with the file it plays
    const int np = (int) was->proc->allSpecs().size();
    for (int i = 0; i < np; ++i)
        if (auto* from = was->proc->param (i))
            if (auto* to = v->proc->param (i))
                if (std::abs (to->getValue() - from->getValue()) > 1.0e-7f) to->setValueNotifyingHost (from->getValue());
    bindVoice (l, *v);                      // (the copy has the model the one before it had, and so its knobs)
    // (made ready at the engine's rate before it asks for its file: a sample that is only held in memory is held at that rate,
    // and a voice still at the rate it was born with would ask for another and, the file gone, be given none)
    if (prepared && files[(size_t) l].isNotEmpty()) { v->proc->setRateAndBufferSizeDetails (sr, maxBlock); v->proc->prepareToPlay (sr, maxBlock); }
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
        if (st == kSlots) ln.holdingNow = false;
    }
    if (! ln.fading[st]) return false;
    if (st == kSlots)
    {
        const float h = ln.holdG.load (std::memory_order_relaxed);
        if (h >= 0.0f)
        {
            // Held between the two by a knob: as much of the one that arrived as the knob says. However the knob jumps,
            // the whole of the way takes a fiftieth of a second at least, so nothing clicks.
            ln.holdingNow = true;
            const int end = ln.holdEnd.load (std::memory_order_relaxed);
            const double least = 0.02 * tempoFor (cur[(size_t) cx::kTempo]) / 60.0;
            const float most = (float) juce::jlimit (0.0, 1.0, beats / least), to = end == 1 ? 1.0f : end == 2 ? 0.0f : std::min (1.0f, h);
            g0 = ln.fadeG[st];
            g1 = g0 + juce::jlimit (-most, most, to - g0);
            ln.fadeG[st] = g1;
            ln.fadeAt[st].store (g1, std::memory_order_relaxed);
            if (end == 1 && g1 >= 1.0f) { ln.fading[st] = false; ln.holdingNow = false; ln.fadeDoneAt[st].store (blockCount.load()); ln.fadeDone[st].store (q); }
            else if (end == 2 && g1 <= 0.0f) ln.holdBack.store (q, std::memory_order_relaxed);
            return true;
        }
        if (ln.holdingNow)
        {
            // the hold is over and the lane given back to the one that was playing: nothing is between two any more
            ln.holdingNow = false; ln.fading[st] = false; ln.fadeG[st] = 1.0f; ln.fadeAt[st].store (1.0f, std::memory_order_relaxed);
            return false;
        }
    }
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
    // a hold that has settled: on the one that arrived (the one that was playing has left, above), or on the one that
    // was playing (the audio thread says the other is at nothing: the lane is given back, and the other let go)
    for (int l = 0; l < kLanes; ++l)
    {
        if (! heldLane[(size_t) l]) continue;
        auto& ln = lane[(size_t) l];
        const int end = ln.holdEnd.load();
        const auto q = ln.fadeSeq[kSlots].load();
        auto* was = ln.voiceOut.load();
        if (was == nullptr || (end == 1 && ln.fadeDone[kSlots].load() == q)) dropHold (l);
        else if (end == 2 && (all || ! prepared || ln.holdBack.load() == q))
        {
            auto* arrived = ln.voice.load();
            ln.voice.store (was);           // (for a moment the lane has the same one twice: the audio thread plays it once)
            ln.voiceOut.store (nullptr);
            retiredVoices.push_back ({ arrived, now });
            files[(size_t) l] = heldFile[(size_t) l];
            dropHold (l);
            stateMoved();
            updateLatency();
        }
    }
    for (size_t i = retiredLaws.size(); i-- > 0;)
        if (all || now >= retiredLaws[i].second + 2 || ! prepared)
        {
            delete retiredLaws[i].first;
            retiredLaws.erase (retiredLaws.begin() + (long) i);
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
    inst->proc->setMotionSeed (motionSeedFor ({ 1, l, slot }));
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
        int ins = 0;
        for (auto& i : lane[(size_t) l].ins) if (auto* in = i.load()) ins += in->proc->getLatencySamples();
        lane[(size_t) l].insLat = ins;
        lat[l] += ins;
        mx = std::max (mx, lat[l]);
    }
    // Every lane is given the longest wait any lane could need, always (holdLat), so the rig's
    // delay does not move with what a recall or a hand puts on a lane.
    mx = std::min (std::max (mx, holdLat), 8000);
    for (int l = 0; l < kLanes; ++l) lane[(size_t) l].delay = juce::jlimit (0, 8000, mx - lat[l]);
    maxLat = mx;
    int ml = 0, before = 0;
    for (size_t k = 0; k < master.size(); ++k) { if (k + 1 < master.size()) before += master[k]->getLatencySamples(); ml += master[k]->getLatencySamples(); }
    masterLat = ml; masterLatBefore = before;
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
    for (auto& given : paths)
    {
        juce::FileInputStream in { juce::File (given) };
        if (! in.openedOk()) continue;
        ok.add (keepFile ? keepFile (given) : given);          // (the library's own copy of it, where one is kept)
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

Pockets::Set Engine::pocketSet (bool shownNow) const
{
    // (the audio thread asks for what sounds now: its own table. The message thread: what is shown, or where it is going.)
    auto v = [&] (int i) { return shownNow ? shownV[(size_t) i].load (std::memory_order_relaxed) : targetV[(size_t) i].load (std::memory_order_relaxed); };
    Pockets::Set s;
    s.amount = v (cx::kPkG); s.clear = v (cx::kPkG + 1); s.speed = v (cx::kPkG + 2); s.returns = v (cx::kPkG + 3);
    for (int l = 0; l < kLanes; ++l)
    {
        s.home[(size_t) l] = v (cx::pk (l, 0)); s.width[(size_t) l] = v (cx::pk (l, 1));
        s.hold[(size_t) l] = v (cx::pk (l, 2)); s.yield[(size_t) l] = v (cx::pk (l, 3));
    }
    return s;
}

// ---------------------------------------------------------------------------------------- setup
sf::Reach Engine::reachOf (int lane, const juce::String& id) const
{
    auto* p = processorFor ({ 0, lane, 0 });
    if (p == nullptr) return sf::Reach::Live;
    const int i = p->paramIndex (id.toRawUTF8());
    if (i < 0 || i >= p->numModuleParams()) return sf::Reach::Live;
    std::string model;
    if (const int mi = p->paramIndex ("model"); mi >= 0)
    {
        const auto& ch = p->allSpecs()[(size_t) mi].choices;
        const int at = juce::roundToInt (p->param (mi)->convertFrom0to1 (p->param (mi)->getValue()));
        if (at >= 0 && at < (int) ch.size()) model = ch[(size_t) at];
    }
    // (the hundred and fourth build) SAMPLE's POSITION is a note's start in REPITCH and STRETCH, but in GRAIN (with NOTES on
    // PITCH) it moves the place the grains are read about at once
    if (id == "sp_pos")
    {
        const auto text = [p] (const char* pid) { const int k = p->paramIndex (pid); return k >= 0 ? p->param (k)->getCurrentValueAsText().toUpperCase() : juce::String(); };
        if (text ("sp_method") == "GRAIN" && text ("sp_notes") == "PITCH") return sf::Reach::Live;
    }
    // (the hundred and eleventh build) SPREAD scatters each grain as it is made, so in GRAIN it is heard at once; and SYNTH
    // reads the key every block, so on SYNTH KEY FOLLOW is heard at once (SAMPLE and KICK FORGE take it as a note begins)
    if (id == "sp_spread")
    {
        const int k = p->paramIndex ("sp_method");
        if (k >= 0 && p->param (k)->getCurrentValueAsText().toUpperCase() == "GRAIN") return sf::Reach::Live;
    }
    if (id == "key_follow" && model == "SYNTH") return sf::Reach::Live;
    return p->allSpecs()[(size_t) i].reachIn (model);
}

SFProcessor* Engine::processorFor (Target t) const
{
    if (t.kind == kMixPage) return nullptr;       // (the mix: no instrument's, the engine's own)
    if (t.kind == 0) return lane[(size_t) t.lane].voice.load()->proc.get();
    if (t.kind == 1) { auto* in = lane[(size_t) t.lane].ins[t.slot].load(); return in != nullptr ? in->proc.get() : nullptr; }
    if (t.kind == 3) return busProc.get();
    if (t.kind == 4) { for (auto& m : master) if (m->getName() == "SF GLUE") return m.get(); return nullptr; }
    return t.slot == 0 ? retA.get() : space[(size_t) spaceWant.load()].get();
}

const KnobDef* Engine::masterCompKnob()
{
    for (auto& st : masterChain()) if (juce::String (st.module) == "GLUE") return &st.knob;
    return nullptr;
}

std::vector<std::string> Engine::boundIds (Target t) const
{
    std::vector<std::string> ids;
    auto add = [&ids] (const KnobDef* k, int n) { for (int i = 0; i < n; ++i) if (k[i].param != nullptr) ids.push_back (k[i].param); };
    if (t.kind == 0) { for (int k = 0; k < kLaneKnobs; ++k) if (const auto& kd = laneKnob (t.lane, k); kd.param != nullptr) ids.push_back (kd.param); }
    else if (t.kind == 1) { const int ty = insType[(size_t) t.lane][(size_t) t.slot]; if (ty >= 0) add (effects()[(size_t) ty].cells, kCells); }
    else if (t.kind == 3) add (drumBus().cells, kCells);
    else if (t.kind == 4) { if (auto* k = masterCompKnob()) add (k, 1); }
    else add ((t.slot == 0 ? returnA() : returnB (spaceWant.load())).cells, kCells);
    return ids;
}

// An EQ's bands as CONFIG lists them, a row a band: a lane's, or the master's (l: kLanes), which has no knobs on a panel.
void Engine::eqList (int l, std::vector<SetupParam>& out) const
{
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto band = eqBand (l, b), d = eqDefault (b);
        const int knob = l < kLanes ? eqKnobOf (b) : -1;
        const juce::String group = "EQ " + juce::String (b + 1) + (knob == 0 ? " LOW" : knob == 1 ? " MID" : knob == 2 ? " HIGH" : "");
        for (int w = 0; w < 4; ++w)
        {
            SetupParam sp;
            sp.id = eqIdOf (b, w); sp.group = group; sp.ownRow = w == 0;
            if (w == 0)
            {
                sp.name = "KIND"; sp.kind = Kind::List;
                for (int k = 0; k < kEqTypes; ++k) sp.choices.add (eqTypeName (k));
                sp.value = (float) band.type / (float) (kEqTypes - 1); sp.def = (float) d.type / (float) (kEqTypes - 1); sp.text = eqTypeName (band.type);
            }
            else if (w == 1)
            {
                sp.name = "FREQ"; sp.kind = Kind::Knob; sp.value = eqHzPlace (band.hz); sp.def = eqHzPlace (d.hz);
                sp.text = band.hz >= 1000.0f ? juce::String (band.hz / 1000.0f, band.hz >= 10000.0f ? 1 : 2) + " KHZ" : juce::String ((int) std::lround (band.hz)) + " HZ";
            }
            else if (w == 2)
            {
                // (a knob band's gain is its knob: shown here as the knob stands)
                sp.name = "GAIN"; sp.kind = Kind::Bipolar;
                sp.value = knob >= 0 ? target (cx::lane (l, cx::kEqLow + knob)) : eqDbPlace (band.db); sp.def = 0.5f;
                sp.text = (band.db > 0.0f ? "+" : "") + juce::String (band.db, 1) + " DB";
            }
            else { sp.name = "WIDTH"; sp.kind = Kind::Knob; sp.value = eqQPlace (band.q); sp.def = eqQPlace (d.q); sp.text = "Q " + juce::String (band.q, 2); }
            out.push_back (sp);
        }
    }
}

// ---- the mix (the ninety-fifth build)
juce::String Engine::mixId (int k)
{
    if (k >= 0 && k < kLanes) return "mix_" + juce::String (lanes()[(size_t) k].slug);
    return k == kMixDub ? "mix_dub" : k == kMixSpace ? "mix_space" : k == kMixBus ? "mix_bus" : juce::String();
}
juce::String Engine::mixName (int k)
{
    if (k >= 0 && k < kLanes) return lanes()[(size_t) k].name;
    return k == kMixDub ? "DUB" : k == kMixSpace ? "SPACE" : k == kMixBus ? "DRUM BUS" : juce::String();
}
int Engine::mixOfId (const juce::String& id)
{
    if (! id.startsWith ("mix_")) return -1;
    for (int k = 0; k < kMixFaders; ++k) if (id == mixId (k)) return k;
    return -1;
}
juce::String Engine::mixText (float place)
{
    const float g = mixGainOf (place);
    if (g <= 1.0e-5f) return "OFF";
    float db = 20.0f * std::log10 (g);
    if (std::abs (db) < 0.05f) db = 0.0f;
    return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " DB";
}

std::vector<Engine::SetupParam> Engine::setupParams (Target t) const { return setupPage (t).list; }

// Where a control stands on its CONFIG page (the ninety-fourth build). Wiley, Nov 19: "Every config page needs to be broken
// down into a 4x4 grids. each grid position can contain a knob or a button. Any knob that's shared between lane configs
// should always be in exactly the same place." One table: a control's cell is its own on every page that has it, whatever
// the model, and a cell whose control is away (on the main panel, or not this model's) is left empty, never filled.
namespace
{
struct CfgCell { const char* id; int cell; };
// a lane, the first grid, LANE (SF-000, the hundred and third build: every lane is one instrument, Lane.cpp, and this is all
// of it that is not an engine's). A row a job: what plays and how the two meet (SPLIT HZ under SPLIT); how it is played; the
// echoes, and the lane filter's RES beside them (FILTER is on the panel); the pocket under the kick, and PLAYS (the hundred
// and twelfth build: HITS or NOTES, on every lane). RUMBLE's SOURCE, what strikes it, was there: it is the MOTION grid's
// last row now (kSourceCell), the one control that moved, for PLAYS to be on LANE where every lane has it.
const CfgCell kLaneCells[] = { { "model", 0 }, { "layer_model", 1 }, { "layer", 2 }, { "split", 3 },
                               { "key_follow", 4 }, { "vel_sens", 5 }, { "mono", 6 }, { "split_hz", 7 },
                               { "echo_prob", 8 }, { "echo_space", 9 }, { "echo_swing", 10 }, { "res", 11 },
                               { "pocket_depth", 12 }, { "pocket_shape", 13 }, { "pocket_post", 14 }, { "plays", 15 } };
constexpr int kSourceCell = 12;         // (RUMBLE's SOURCE: the second grid's, beside LANE)
// (a lane's MOTION is the second grid, beside LANE; the engines follow, the model's from the third, the layer's after it)
// KICK FORGE's own (the ninety-ninth build): two grids of it. Cells 0 to 15, the third grid, BODY: what is struck and
// how its pitch and level fall; 16 to 31, the fourth, IMPACT AND GROUND: the front of the hit, then the floor under it.
// The same two whether it is the kick's model or its layer: no other kick model has knobs of its own.
const CfgCell kForgeCells[] = { { "fg_char", 0 }, { "fg_body", 1 }, { "fg_tone", 2 }, { "fg_low", 3 },
                                { "fg_hang", 4 }, { "fg_curve", 5 }, { "fg_hold", 6 }, { "fg_hdec", 7 },
                                { "fg_phase", 8 }, { "fg_flip", 9 },
                                { "fg_impact", 16 }, { "fg_pos", 17 }, { "fg_ilen", 18 }, { "fg_ipitch", 19 },
                                { "fg_icut", 20 }, { "fg_iwidth", 21 }, { "fg_vary", 22 }, { "fg_solo", 23 },
                                { "fg_ground", 24 }, { "fg_glen", 25 }, { "fg_gdark", 26 }, { "fg_gduck", 27 },
                                { "fg_grec", 28 }, { "fg_gwidth", 29 },
                                // (the hundred and second build: the Forge on every lane. Where the lane is not KICK these
                                // five are the Forge's own, which on KICK are the lane's: PUNCH, PITCH TIME and CLICK at the
                                // body's foot, TUNE and DRIVE beside them, rings marked ON PANEL while it is the model)
                                { "fg_punch", 10 }, { "fg_ptime", 11 }, { "fg_click", 12 }, { "fg_tune", 14 }, { "fg_drive", 15 } };
// (SF-000, the hundredth build) SAMPLE, the engine every lane has: all of its controls that are not on the panel, on one
// view, in two grids, the same on every lane and whether it is the model or the layer. SOURCE (the third grid): what of the
// file, and how it is cut. PLAY (the fourth): how it is read. (Wiley, Nov 21: "keep the config controls as consolidated
// to the fewest pages possible".)
const CfgCell kSampCells[] = { { "sp_start", 0 }, { "sp_end", 1 }, { "sp_fin", 2 }, { "sp_fout", 3 },
                               { "sp_gain", 4 }, { "sp_norm", 5 }, { "sp_rev", 6 }, { "sp_root", 7 },
                               { "sp_loop", 8 }, { "sp_lstart", 9 }, { "sp_lend", 10 }, { "sp_xfade", 11 },
                               { "sp_sliceby", 12 }, { "sp_count", 13 }, { "sp_sens", 14 }, { "sp_base", 15 },
                               { "sp_method", 16 }, { "sp_notes", 17 }, { "sp_play", 18 }, { "sp_choke", 19 },
                               { "sp_cycle", 20 }, { "sp_vels", 21 }, { "sp_vary", 22 },
                               { "sp_rate", 24 }, { "sp_pspread", 25 }, { "sp_shape", 26 }, { "sp_gsync", 27 },
                               { "sp_bars", 28 }, { "sp_warp", 29 }, { "sp_lock", 30 } };
// and its eight that the panel has while it is the model: on the third view's left (a ring marked ON PANEL there, or, while
// SAMPLE is the layer and the panel is the model's, the knobs themselves). What another engine has of its own, while SAMPLE
// plays beside it, is on a fourth view (the old sample controls, GRANULAR's and WAVETABLE's TABLE's, on its right).
const CfgCell kSampPanel[] = { { "sp_tune", 0 }, { "sp_pos", 1 }, { "sp_size", 2 }, { "sp_scan", 3 },
                               { "sp_spread", 4 }, { "sp_attack", 5 }, { "sp_release", 6 }, { "sp_drive", 7 } };
// (SF-000, the hundred and second build) SYNTH, the other engine every lane has: all of it on four grids, the same on every
// lane and whether it is the model or the layer, its panel's eight in their own cells (rings marked ON PANEL while it is the
// model). OSCILLATORS: A's row, B's row, how they meet (and TUNE), what is beside them. WAVE MOTION: SCAN and what it does at
// an end, STEPS and W DEPTH; the wave envelope's four stages, a time and a place each; its loop, and velocity and the key.
// FILTER AND ENVELOPES: the filter's row, ENV AMT and ENV DECAY under CUTOFF and RES as on a 303 (DRIVE at the row's end,
// where the synths' SOUND has it), the amp envelope with ACCENT at its end, then PITCH ENV and BURSTS. VOICE AND BODY: how
// many and how they move, unison, BODY, and the patch, SPACE and GAIN. Its first two on the second view, the third beside
// MOTION, the fourth on the fourth view; beside SAMPLE (both playing on one lane), on the fourth and fifth views.
const CfgCell kSynCells[] = { { "sy_table_a", 0 }, { "sy_shape", 1 }, { "sy_warp", 2 }, { "sy_warp_as", 3 },
                              { "sy_table_b", 4 }, { "sy_shape_b", 5 }, { "sy_pitch_b", 6 }, { "sy_fine_b", 7 },
                              { "sy_mix", 8 }, { "sy_fm", 9 }, { "sy_sync", 10 }, { "sy_tune", 11 },
                              { "sy_sub", 12 }, { "sy_noise", 13 }, { "sy_color", 14 }, { "sy_phase", 15 },
                              { "sy_scan", 16 }, { "sy_scan_end", 17 }, { "sy_steps", 18 }, { "sy_wdepth", 19 },
                              { "sy_wt1", 20 }, { "sy_wp1", 21 }, { "sy_wt2", 22 }, { "sy_wp2", 23 },
                              { "sy_wt3", 24 }, { "sy_wp3", 25 }, { "sy_wt4", 26 }, { "sy_wp4", 27 },
                              { "sy_wloop", 28 }, { "sy_wvel", 29 }, { "sy_wkey", 30 },
                              { "sy_circuit", 32 }, { "sy_cutoff", 33 }, { "sy_res", 34 }, { "sy_keytrack", 35 },
                              { "sy_envamt", 36 }, { "sy_decay", 37 }, { "sy_mattack", 38 }, { "sy_drive", 39 },
                              { "sy_attack", 40 }, { "sy_sustain", 41 }, { "sy_release", 42 }, { "sy_accent", 43 },
                              { "sy_pitch_env", 44 }, { "sy_pitch_time", 45 }, { "sy_bursts", 46 }, { "sy_burst_gap", 47 },
                              { "sy_voices", 48 }, { "sy_glide", 49 }, { "sy_slide", 50 }, { "sy_chord", 51 },
                              { "sy_unison", 52 }, { "sy_detune", 53 }, { "sy_width", 54 }, { "sy_drift", 55 },
                              { "sy_body", 56 }, { "sy_body_mix", 57 }, { "sy_body_decay", 58 }, { "sy_body_tone", 59 },
                              { "sy_patch", 60 }, { "sy_space", 61 }, { "sy_gain", 62 } };
// an effect or a return, the second grid, TIME: what the delays and reverbs share
const CfgCell kTimeCells[] = { { "__model", 0 }, { "throw", 1 }, { "freeze", 2 }, { "mix", 3 },
                               { "width", 4 }, { "lo_cut", 5 }, { "input", 6 }, { "dry", 7 },
                               { "duck", 8 }, { "duck_shape", 9 }, { "duck_for", 10 }, { "predelay", 11 } };
// a convolution's BANDS (its fifth grid): the switch and the two crossovers, then a row a band
const CfgCell kBandCells[] = { { "bands", 0 }, { "bx_low", 1 }, { "bx_high", 2 },
                               { "b1_place", 4 }, { "b1_level", 5 }, { "b1_decay", 6 }, { "b1_size", 7 },
                               { "b2_place", 8 }, { "b2_level", 9 }, { "b2_decay", 10 }, { "b2_size", 11 },
                               { "b3_place", 12 }, { "b3_level", 13 }, { "b3_decay", 14 }, { "b3_size", 15 } };
// the compressor, on the master and as an insert (where the first eight are its panel's)
const CfgCell kCompCells[] = { { "threshold", 0 }, { "attack", 1 }, { "release", 2 }, { "ratio", 3 },
                               { "sc_hpf", 4 }, { "makeup", 5 }, { "mix", 6 }, { "level", 7 },       // (the hundred and sixth build: MIX and LEVEL where every insert has them)
                               { "knee", 8 }, { "auto", 9 }, { "start", 10 } };
const CfgCell kBusCells[]  = { { "damp", 0 }, { "boom_tune", 1 }, { "boom_decay", 2 }, { "mix", 3 },
                               { "type", 4 }, { "comp", 5 }, { "to_key", 6 }, { "level", 7 } };
template <size_t N> int cfgCellIn (const CfgCell (&table)[N], const juce::String& id) { for (auto& c : table) if (id == c.id) return c.cell; return -1; }
}

Engine::SetupPage Engine::setupPage (Target t) const
{
    SetupPage page;
    std::vector<SetupParam>& out = page.list;
    if (t.kind == kMixPage)
    {
        // THE MIX: a fader a lane in the lanes' order, a row of the returns and the drum bus under them (one grid, sixteen
        // cells: an E16's sixteen), and then the master's EQ, which the screen draws as its graph
        for (int k = 0; k < kMixFaders; ++k)
        {
            SetupParam sp;
            sp.id = mixId (k); sp.name = mixName (k); sp.group = "MIX"; sp.kind = Kind::Knob;
            sp.value = mixFader (k); sp.def = kMixUnity; sp.text = mixText (sp.value);
            sp.grid = 0; sp.cell = k < kLanes ? k : 12 + (k - kLanes);
            out.push_back (sp);
        }
        eqList (kLanes, out);
        page.title[0] = "MIX"; page.title[1] = "LOUDNESS";
        return page;
    }
    auto* p = processorFor (t);
    if (p == nullptr) return page;
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
    auto made = [&] (int i)
    {
        const auto& s = specs[(size_t) i];
        auto* prm = p->param (i);
        SetupParam sp;
        sp.id = s.id; sp.name = s.name; sp.value = prm->getValue(); sp.def = prm->getDefaultValue(); sp.text = prm->getCurrentValueAsText();
        if (s.type == sf::PType::Choice) { sp.kind = Kind::List; for (auto& c : s.choices) sp.choices.add (c); }
        else if (s.type == sf::PType::Toggle) sp.kind = Kind::Toggle;
        else if (s.type == sf::PType::Momentary) sp.kind = Kind::Hold;
        else sp.kind = s.unit == "BIP" || (s.min < 0.0f && s.max > 0.0f && std::abs (s.min + s.max) < 1e-3f) ? Kind::Bipolar : Kind::Knob;
        return sp;
    };
    // Which box each control belongs in. (SF-000, the hundred and third build) A lane: LANE, then its model's engine, then
    // its layer's, which is listed only while the layer is another engine and LAYER is up (the same engine as the model
    // would play nothing more; at none, nothing is heard of it): what is listed does something. (Everything is still kept
    // in snapshots.)
    const bool lane = t.kind == 0;
    auto textOf = [&] (const char* id) { const int i = p->paramIndex (id); return i >= 0 ? p->param (i)->getCurrentValueAsText().toUpperCase() : juce::String(); };
    const juce::String model = textOf ("model"), layerModel = textOf ("layer_model");
    const bool layerShown = lane && layerModel.isNotEmpty() && layerModel != model
                         && [&] { const int i = p->paramIndex ("layer"); return i >= 0 && p->param (i)->getValue() > 0.0f; }();
    auto engineOf = [] (const juce::String& id) -> juce::String
    {
        return id.startsWith ("sp_") ? "SAMPLE" : id.startsWith ("sy_") ? "SYNTH" : id.startsWith ("fg_") ? "KICK FORGE" : juce::String();
    };
    auto groupOf = [&] (const sf::ParamSpec& s) -> juce::String
    {
        const juce::String id (s.id);
        if (lane)
        {
            if (id.startsWith ("sp_cut") || id.startsWith ("sp_ws") || id.startsWith ("sp_wd")) return {};      // (SAMPLE's markers: the display's)
            if (const juce::String e = engineOf (id); e.isNotEmpty()) return e == model ? "MODEL" : layerShown && e == layerModel ? "LAYER" : juce::String();
            return "LANE";
        }
        if (id.startsWith ("smp_cut") && id != "smp_cuts") return {};    // (a slice marker: the display's to move, not a knob's)
        if (t.kind == 3 && id.startsWith ("in_")) return "LANES";       // (which lanes play through the drum bus)
        if (id == "model") return "MODEL";
        if (s.inEngine) return "ENGINE";
        if (s.inSample) return "SAMPLE";
        if (s.inLayer) return "LAYER";
        if (id == "bands" || id.startsWith ("bx_")) return id == "bands" || textOf ("bands") == "ON" ? "BANDS" : juce::String();       // (a convolution's three bands: listed while BANDS is on)
        if (id.startsWith ("b1_") || id.startsWith ("b2_") || id.startsWith ("b3_"))
            return textOf ("bands") != "ON" ? juce::String() : id.startsWith ("b1_") ? "LOW BAND" : id.startsWith ("b2_") ? "MID BAND" : "HIGH BAND";
        if (id.startsWith ("pocket_")) return "POCKET";
        if (id.startsWith ("echo_")) return "ECHO";
        if (id == "alloc") return {};
        if (s.setup) return "RIG";
        return "EFFECT";
    };
    for (auto& sp : out) sp.group = "MODEL";                // (SPACE's MODEL, put in above)
    // The order, the same on every panel (the eighty-seventh build). First what the sound is: MODEL with what only that
    // model has beside it, LAYER, and the knobs a model's own took the places of. Then what is always there, in one
    // order. Then, at the foot, what comes and goes with a setting (a sample's controls, a convolution's bands).
    // Nothing is listed ahead of the control that brings it, so turning a knob never moves that knob or anything
    // before it; and only MODEL and LAYER MODEL change the first part, which the screen gives rows of its own.
    // (In a box: the menu that chooses first, then FM's ALGORITHM and FEEDBACK and WAVETABLE's TABLE, then the rest.)
    // Within a box: the order the module lists its controls in, but for the few every panel has in one order, so the
    // hand finds them in one place: the model before what only that model has (and the control of its own that says
    // how the rest are read first of those); on an effect THROW, FREEZE and MIX first, and LO CUT and what ducks it
    // (INPUT, DUCK, DUCK SHAPE, DUCK FOR) last; on a drum VEL SENS last.
    const bool effect = t.kind == 1 || t.kind == 2 || t.kind == 4;
    auto rank = [effect] (const juce::String& id)
    {
        if (id == "model" || id == "layer_model") return 0;
        if (id == "table" || id == "fm_algo" || id == "fm_feedback") return 1;
        if (effect)
        {
            static const char* const first[] = { "throw", "freeze", "mix" };
            static const char* const last[] = { "lo_cut", "input", "duck", "duck_shape", "duck_for" };
            for (int k = 0; k < 3; ++k) if (id == first[k]) return 2 + k;
            for (int k = 0; k < 5; ++k) if (id == last[k]) return 6 + k;
        }
        return id == "vel_sens" ? 11 : 5;
    };
    auto boxes = [&] (std::initializer_list<const char*> groups, int section)
    {
     for (const char* g : groups)
     {
        std::vector<int> in;
        for (int i = 0; i < p->numModuleParams(); ++i)
        {
            const auto& s = specs[(size_t) i];
            if (std::find (bound.begin(), bound.end(), s.id) == bound.end() && groupOf (s) == g) in.push_back (i);
        }
        std::stable_sort (in.begin(), in.end(), [&] (int a, int b) { return rank (specs[(size_t) a].id) < rank (specs[(size_t) b].id); });
        for (int i : in)
        {
            auto sp = made (i);
            sp.group = g; sp.section = section;
            if (t.kind == 3 && sp.group == "EFFECT") sp.group = "DRUM BUS";
            if (sp.group == "LANES")
            {
                // by the lane's own name, and a row of their own
                const int l = sp.id.fromFirstOccurrenceOf ("in_", false, false).getIntValue() - 1;
                if (l < 0 || l >= kLanes) continue;
                sp.name = lanes()[(size_t) l].name; sp.ownRow = l == 0;
            }
            out.push_back (sp);
        }
     }
    };
    if (lane) boxes ({ "LANE", "MODEL", "LAYER" }, 0);
    else boxes ({ "MODEL", "LAYER", "SAMPLE", "EFFECT", "ENGINE", "POCKET", "ECHO", "LANES", "RIG" }, 1);
    // MOTION, on an instrument, an effect or a return: two rows of their own, after everything that is always there
    {
        for (int k = 1; k <= SFProcessor::kMotions; ++k)
        {
            static const char* const tail[4] = { "_src", "_rate", "_target", "_depth" };
            static const char* const names[4] = { "WAVE", "SPEED", "TARGET", "AMOUNT" };
            for (int j = 0; j < 4; ++j)
            {
                const int i = p->paramIndex ("m" + std::to_string (k) + tail[j]);
                if (i < 0) continue;
                auto sp = made (i);
                sp.name = names[j]; sp.group = "MOTION " + juce::String (k); sp.ownRow = j == 0 && k == 1; sp.section = 1;        // (the two side by side, on a row of their own)
                out.push_back (sp);         // (TARGET's list is made at the foot, once it is known what the page lists)
            }
        }
    }
    boxes ({ "BANDS", "LOW BAND", "MID BAND", "HIGH BAND" }, 2);
    // (the hundred and seventh build) DUB's TO SPACE: how much of what DUB sounds goes on into SPACE, the echoes into the
    // reverb. The engine's own, not the delay's (an insert's DELAY has none)
    if (t.kind == 2 && t.slot == 0)
    {
        SetupParam sp;
        sp.id = kToSpaceId; sp.name = "TO SPACE"; sp.group = "TIME"; sp.kind = Kind::Knob;
        sp.value = dubToSpace.load(); sp.def = 0.0f; sp.text = juce::String (juce::roundToInt (sp.value * 100.0f)) + "%";
        out.push_back (sp);
    }
    // the lane's EQ, a row a band (the screen draws them as a graph, and the E16 has them as its last pages)
    if (lane) eqList (t.lane, out);
    // MOTION's TARGET, what it can move: the panel's own knobs first, as the panel has them, then the rest of what this
    // page lists, and nothing else (the ninety-third build: it listed every control the instrument has, another model's
    // with them. Wiley, Nov 19: "the list of motion targets lists targets that aren't even visible control parameters for
    // the selected model."). What a MOTION already moves stays on its list while it moves it, seen or not.
    {
        const auto seen = shownParams (*p, bound, out);
        for (auto& sp : out)
        {
            if (! (isMotionId (sp.id) && sp.id.endsWith ("_target"))) continue;
            const int i = p->paramIndex (sp.id.toStdString());
            if (i < 0) continue;
            juce::StringArray shown;
            const int now = (int) std::lround (p->param (i)->convertFrom0to1 (p->param (i)->getValue()));
            const auto order = motionOrder (t, *p, seen, now, &shown);
            int at = 0;
            for (size_t q = 0; q < order.size(); ++q) if (order[q] == now) at = (int) q + 1;
            sp.choices.clear(); sp.choices.add ("OFF"); sp.choices.addArray (shown);
            sp.value = order.empty() ? 0.0f : (float) at / (float) order.size(); sp.def = 0.0f; sp.text = sp.choices[at];
        }
    }
    // ---- each to its cell (the ninety-fourth build)
    {
        const juce::String module = lane ? juce::String (lanes()[(size_t) t.lane].module) : p->getName().fromFirstOccurrenceOf ("SF ", false, false);
        const bool comp = ! lane && module == "GLUE", bus = t.kind == 3;
        auto has = [&] (const juce::String& id) { const int i = p->paramIndex (id.toStdString()); return i >= 0 && i < p->numModuleParams(); };
        auto isBound = [&] (const juce::String& id) { return std::find (bound.begin(), bound.end(), id.toStdString()) != bound.end(); };
        auto band = [] (const juce::String& id) { return id == "bands" || id.startsWith ("bx_") || id.startsWith ("b1_") || id.startsWith ("b2_") || id.startsWith ("b3_"); };
        // (SF-000, the hundred and third build) A lane's engines, a unit of grids each, every control in one cell of its unit
        // whichever lane and whether it is the model or the layer: the model's from the third grid (the second view), the
        // layer's from the next view after it. SYNTH: OSCILLATORS, WAVE MOTION, FILTER AND ENVELOPES, VOICE AND BODY.
        // SAMPLE: SOURCE, PLAY, and SOUND (its panel's eight, as the layer). KICK FORGE: BODY, IMPACT AND GROUND.
        // (SAMPLE's third, SOUND, is its panel's eight, which are on the panel while it is the model: it has the grid only as
        // the layer, so that no view is cells kept and nothing to turn)
        auto gridsOf = [] (const juce::String& e, bool layer) { return e == "SYNTH" ? 4 : e == "SAMPLE" ? (layer ? 3 : 2) : e == "KICK FORGE" ? 2 : 0; };
        const int modelAt = 2, layerAt = modelAt + (gridsOf (model, false) + 1) / 2 * 2;
        auto unitCell = [] (const juce::String& e, const juce::String& id) -> int       // (grid within the unit * 16 + cell)
        {
            if (e == "SYNTH") return cfgCellIn (kSynCells, id);
            if (e == "KICK FORGE") return cfgCellIn (kForgeCells, id);
            if (e == "SAMPLE") { if (const int c = cfgCellIn (kSampCells, id); c >= 0) return c; if (const int c = cfgCellIn (kSampPanel, id); c >= 0) return 32 + c; }
            return -1;
        };
        // an effect's own: the cells of its first grid in the order the module lists them, counted over all it could ever
        // list there, so a control that comes and goes takes no other's cell
        std::vector<juce::String> own;
        if (! lane && ! comp && ! bus)
            for (int i = 0; i < p->numModuleParams(); ++i)
            {
                const juce::String id (specs[(size_t) i].id);
                if (specs[(size_t) i].hidden || isBound (id) || band (id) || id == "alloc" || id.startsWith ("smp_cut") || cfgCellIn (kTimeCells, id) >= 0) continue;
                own.push_back (id);
            }
        struct At { int grid, cell; };
        auto placeOf = [&] (const juce::String& id, const juce::String& group) -> At
        {
            juce::ignoreUnused (group);
            if (isMotionId (id)) return { lane ? 1 : 5, (id[1] == '2' ? 4 : 0) + (id.endsWith ("_src") ? 0 : id.endsWith ("_rate") ? 1 : id.endsWith ("_target") ? 2 : 3) };
            if (lane)
            {
                if (const int c = cfgCellIn (kLaneCells, id); c >= 0) return { 0, c };
                if (id == "source") return { 1, kSourceCell };
                const juce::String e = engineOf (id);
                const int base = e.isEmpty() ? -1 : e == model ? modelAt : layerShown && e == layerModel ? layerAt : -1;
                if (const int c = base < 0 ? -1 : unitCell (e, id); c >= 0) return { base + c / 16, c % 16 };
                return { 0, -1 };
            }
            if (band (id)) return { 4, cfgCellIn (kBandCells, id) };
            if (comp) return { 0, cfgCellIn (kCompCells, id) };
            if (bus)
            {
                if (id.startsWith ("in_")) { const int l = id.fromFirstOccurrenceOf ("in_", false, false).getIntValue() - 1; return { 1, l >= 0 && l < 16 ? l : -1 }; }
                return { 0, cfgCellIn (kBusCells, id) };
            }
            if (const int c = cfgCellIn (kTimeCells, id); c >= 0) return { 1, c };
            if (id == kToSpaceId) return { 1, 12 };         // (DUB's: the TIME grid's last row, first)
            const auto it = std::find (own.begin(), own.end(), id);
            return { 0, it != own.end() && it - own.begin() < 16 ? (int) (it - own.begin()) : -1 };
        };
        bool taken[kCfgGrids][16] = {};
        int live = 0;
        for (auto& sp : out)
        {
            if (sp.group.startsWith ("EQ ")) break;         // (the EQ's are last, and the screen's to draw)
            ++live;
            const At at = placeOf (sp.id, sp.group);
            if (at.cell >= 0 && ! taken[at.grid][at.cell]) { sp.grid = at.grid; sp.cell = at.cell; taken[at.grid][at.cell] = true; }
        }
        // (one with no cell of its own, which the checks say there is none of: the first free cell of its grid, so that it
        // is at least on the page)
        for (int i = 0; i < live; ++i)
        {
            auto& sp = out[(size_t) i];
            if (sp.cell >= 0) continue;
            ++page.unplaced;
            const int first = placeOf (sp.id, sp.group).grid;
            for (int g = 0; g < kCfgGrids && sp.cell < 0; ++g)
                for (int c = 0; c < 16 && sp.cell < 0; ++c)
                    if (const int grid = (first + g) % kCfgGrids; ! taken[grid][c]) { sp.grid = grid; sp.cell = c; taken[grid][c] = true; }
        }
        // the cells kept for what is away: on the main panel for now, or not this model's or this setting's
        auto ghost = [&] (int grid, int cell, const juce::String& id, bool onMain)
        {
            const int i = p->paramIndex (id.toStdString());
            if (i < 0 || i >= p->numModuleParams() || cell < 0 || taken[grid][cell]) return;
            const auto shape = made (i);
            page.ghosts.push_back ({ grid, cell, id, shape.name, shape.kind, onMain });
            taken[grid][cell] = true;
        };
        if (lane)
        {
            // (a lane's cells kept: LANE's every one it has; an engine's, while it is listed, where its panel has them)
            for (auto& c : kLaneCells) if (has (c.id)) ghost (0, c.cell, c.id, isBound (c.id));
            if (has ("source")) ghost (1, kSourceCell, "source", false);
            auto unitGhosts = [&] (const juce::String& e, int base, bool layer)
            {
                if (base < 0 || e.isEmpty()) return;
                for (int i = 0; i < p->numModuleParams(); ++i)
                    if (const juce::String id (specs[(size_t) i].id); engineOf (id) == e)
                        if (const int c = unitCell (e, id); c >= 0 && c / 16 < gridsOf (e, layer)) ghost (base + c / 16, c % 16, id, isBound (id));
            };
            unitGhosts (model, modelAt, false);
            if (layerShown) unitGhosts (layerModel, layerAt, true);
        }
        else
        {
            if (comp) for (auto& c : kCompCells) if (has (c.id)) ghost (0, c.cell, c.id, isBound (c.id));
            for (auto& c : kBandCells) if (has (c.id)) ghost (4, c.cell, c.id, false);
            if (! comp && ! bus)
            {
                for (auto& c : kTimeCells) if (has (c.id) && ! isBound (c.id)) ghost (1, c.cell, c.id, false);
                for (size_t k = 0; k < own.size() && k < 16; ++k) ghost (0, (int) k, own[k], false);
            }
        }
        // the grids' names
        if (lane)
        {
            page.title[0] = "LANE"; page.title[1] = "MOTION";
            auto unitTitles = [&] (const juce::String& e, int base, bool layer)
            {
                static const char* const syn[] = { "OSCILLATORS", "WAVE MOTION", "FILTER AND ENVELOPES", "VOICE AND BODY" };
                static const char* const smp[] = { "SOURCE", "PLAY", "SOUND" };
                static const char* const frg[] = { "BODY", "IMPACT AND GROUND" };
                const char* const* names = e == "SYNTH" ? syn : e == "SAMPLE" ? smp : frg;
                for (int k = 0; k < gridsOf (e, layer); ++k) { page.title[base + k] = (layer ? "LAYER " : "") + juce::String (names[k]); page.says[base + k] = e; }
            };
            unitTitles (model, modelAt, false);
            if (layerShown) unitTitles (layerModel, layerAt, true);
            for (int i = 0; i < live; ++i) { auto& sp = out[(size_t) i]; if (sp.grid >= 0 && sp.grid != 1) sp.group = page.title[sp.grid].isNotEmpty() ? page.title[sp.grid] : sp.group; }
        }
        else
        {
            page.title[0] = comp ? "COMPRESSOR" : bus ? "BUS" : "EFFECT";
            if (bus) page.title[1] = "LANES"; else if (! comp) page.title[1] = "TIME";
            if (p->paramIndex ("bands") >= 0) { page.title[4] = "BANDS"; page.rowTag[4][1] = "LOW"; page.rowTag[4][2] = "MID"; page.rowTag[4][3] = "HIGH"; }
            page.title[5] = "MOTION";
        }
        page.rowTag[lane ? 1 : 5][0] = "1"; page.rowTag[lane ? 1 : 5][1] = "2";
        // (a grid with nothing in it says why; an effect with nothing the delays and reverbs share has no TIME)
        {
            bool any[kCfgGrids] = {};
            for (int g = 0; g < kCfgGrids; ++g) for (int c = 0; c < 16; ++c) any[g] = any[g] || taken[g][c];
            // (the fourth view only where there is something on it)
            // (and the fifth and sixth)
            for (int v = 3; v < kCfgGrids / 2; ++v)
            {
                const int a = 2 * v, b = a + 1;
                if (! any[a] && ! any[b]) { page.title[a] = page.title[b] = {}; page.says[a] = page.says[b] = {}; }
                else if (! any[b]) { page.title[b] = {}; page.says[b] = {}; }
            }
            if (! lane)
            {
                if (! any[1]) page.title[1] = {};
                if (! any[0]) page.note[0] = "ALL OF IT IS ON THE PANEL.";
            }
        }
        // the list: a grid at a time, a row at a time (the E16 holds them in this order), and then the EQ's
        std::stable_sort (out.begin(), out.begin() + live, [] (const SetupParam& a, const SetupParam& b) { return a.grid * 16 + a.cell < b.grid * 16 + b.cell; });
    }
    return page;
}

// The instrument's own controls that the panel or its CONFIG page shows now (their places in the module's list).
std::vector<int> Engine::shownParams (SFProcessor& p, const std::vector<std::string>& bound, const std::vector<SetupParam>& listed)
{
    std::vector<int> seen;
    auto add = [&] (int i) { if (i >= 0 && i < p.numModuleParams() && std::find (seen.begin(), seen.end(), i) == seen.end()) seen.push_back (i); };
    for (auto& b : bound) add (p.paramIndex (b));
    for (auto& sp : listed) if (! isMotionId (sp.id)) add (p.paramIndex (sp.id.toStdString()));
    return seen;
}

bool Engine::isMotionId (const juce::String& id)
{
    return id.length() >= 5 && id[0] == 'm' && (id[1] == '1' || id[1] == '2') && id[2] == '_'
        && (id.endsWith ("_src") || id.endsWith ("_rate") || id.endsWith ("_target") || id.endsWith ("_depth"));
}

std::vector<int> Engine::motionOrder (Target t, SFProcessor& p, const std::vector<int>& seen, int keep, juce::StringArray* names) const
{
    const auto& targets = p.motionTargetParams();           // TARGET's choice c (1 on) moves module parameter targets[c - 1]
    const auto& specs = p.allSpecs();
    // the knobs on its own panel: a lane's twelve, an effect's or a return's cells
    const KnobDef* defs = nullptr; int count = 0;
    KnobDef laneNow[kLaneKnobs];
    if (t.kind == 0) { for (int k = 0; k < kLaneKnobs; ++k) laneNow[k] = laneKnob (t.lane, k); defs = laneNow; count = kLaneKnobs; }
    else if (t.kind == 1) { const int ty = insType[(size_t) t.lane][(size_t) t.slot]; if (ty >= 0) { defs = effects()[(size_t) ty].cells; count = kCells; } }
    else if (t.kind == 3) { defs = drumBus().cells; count = kCells; }
    else if (t.kind == 4) { defs = masterCompKnob(); count = defs != nullptr ? 1 : 0; }
    else { defs = (t.slot == 0 ? returnA() : returnB (spaceWant.load())).cells; count = kCells; }
    std::vector<int> order;
    auto add = [&] (int choice, const juce::String& name) { if (std::find (order.begin(), order.end(), choice) == order.end()) { order.push_back (choice); if (names != nullptr) names->add (name); } };
    for (int k = 0; k < count; ++k)
        if (defs[k].param != nullptr && defs[k].label != nullptr)
        {
            const int pi = p.paramIndex (defs[k].param);
            for (size_t c = 0; c < targets.size(); ++c) if (targets[c] == pi) add ((int) c + 1, defs[k].label);
        }
    // (then the rest of what is shown; and what this MOTION moves now, shown or not, so that the list says what it is)
    for (size_t c = 0; c < targets.size(); ++c)
        if ((int) c + 1 == keep || std::find (seen.begin(), seen.end(), targets[c]) != seen.end()) add ((int) c + 1, juce::String (specs[(size_t) targets[c]].name));
    return order;
}

const std::vector<int>& Engine::motionParamsOf (const juce::String& module)
{
    static std::mutex lock;
    static std::map<juce::String, std::vector<int>> known;
    const std::lock_guard<std::mutex> g (lock);
    if (const auto it = known.find (module); it != known.end()) return it->second;
    SFProcessor p (sf::makeModule (module.toRawUTF8()));
    return known[module] = p.motionTargetParams();
}

float Engine::motionTo (const juce::String& module, float place)
{
    const auto& t = motionParamsOf (module);
    const int c = (int) std::lround (place * (float) t.size());
    return c <= 0 || c > (int) t.size() ? 0.0f : (float) (t[(size_t) c - 1] + 1) / 1024.0f;
}

float Engine::motionPlaceOfTo (const juce::String& module, float to)
{
    const auto& t = motionParamsOf (module);
    const int pi = (int) std::lround (to * 1024.0f) - 1;
    for (size_t c = 0; c < t.size(); ++c) if (t[c] == pi) return (float) (c + 1) / (float) t.size();
    return 0.0f;
}

float Engine::motionPlaceFrom (const juce::String& module, float place, juce::int64 takenMs, int lists)
{
    if (place <= 0.0f) return place;
    // How long each module's list has been, and since when (the second its build was made). A module not here has had
    // one length. The last of each is what it was until the eighty-fifth build, from which a snapshot says the parameter.
    struct Was { juce::int64 since; int count; };
    static const juce::int64 grains = 1794981414, percSample = 1795035064, bands = 1794592415;
    static const std::map<juce::String, std::vector<Was>> history {
        { "TERROIR", { { 0, 9 }, { bands, 20 } } } };
    const auto it = history.find (module);
    if (it == history.end()) return place;
    const int now = (int) motionParamsOf (module).size();
    // (when it was taken; and no later than its own mark allows: lists 1 is from before GRANULAR's knobs, 2 from before PERC's SAMPLE)
    const juce::int64 mark = lists < 2 ? grains - 1 : lists < 3 ? percSample - 1 : std::numeric_limits<juce::int64>::max();
    const juce::int64 when = std::min (takenMs > 0 ? takenMs / 1000 : mark, mark);
    auto fits = [place] (int count)
    {
        const float x = place * (float) count;
        const int c = (int) std::lround (x);
        return std::abs (x - (float) c) < 0.004f && c >= 1 && c <= count ? c : 0;
    };
    // the latest length it had by then that the place falls on; failing that (a clock that was wrong), any it falls on
    for (int pass = 0; pass < 2; ++pass)
        for (auto w = it->second.rbegin(); w != it->second.rend(); ++w)
        {
            if (pass == 0 && w->since > when) continue;
            if (const int c = fits (w->count); c > 0) return now > 0 ? (float) c / (float) now : place;
        }
    return place;
}

void Engine::motionWrite (SetupState& st, const juce::String& module)
{
    if (module.isEmpty()) return;
    for (int k = 1; k <= SFProcessor::kMotions; ++k)
    {
        const juce::String id = "m" + juce::String (k) + "_target", to = "m" + juce::String (k) + "_to";
        float place = -1.0f; bool has = false;
        for (auto& kv : st) { if (kv.first == id) place = kv.second; has = has || kv.first == to; }
        if (place >= 0.0f && ! has) st.push_back ({ to, motionTo (module, place) });
    }
}

void Engine::motionRead (SetupState& st, const juce::String& module, juce::int64 takenMs, int lists)
{
    if (module.isEmpty()) return;
    // (SF-000, the hundred and third build) a lane from before it was one instrument (lists 7): what its MOTION moved was a
    // control of an instrument that is gone, so it moves nothing now
    const bool gone = lists < 7 && [&] { for (auto& d : lanes()) if (module == d.module) return true; return false; }();
    for (int k = 1; k <= SFProcessor::kMotions; ++k)
    {
        const juce::String id = "m" + juce::String (k) + "_target", to = "m" + juce::String (k) + "_to";
        if (gone) { for (auto it = st.begin(); it != st.end(); ++it) if (it->first == to) { st.erase (it); break; } for (auto& kv : st) if (kv.first == id) kv.second = 0.0f; continue; }
        float said = -1.0f;
        for (auto it = st.begin(); it != st.end(); ++it) if (it->first == to) { said = it->second; st.erase (it); break; }
        for (auto& kv : st)
            if (kv.first == id) kv.second = said >= 0.0f ? motionPlaceOfTo (module, said) : motionPlaceFrom (module, kv.second, takenMs, lists);
    }
}

Engine::SetupState Engine::motionOff (Target t) const
{
    SetupState st;
    if (auto* p = processorFor (t))
        for (int k = 1; k <= SFProcessor::kMotions; ++k)
            for (const char* tail : { "_src", "_rate", "_target", "_depth" })
                if (const int i = p->paramIndex ("m" + std::to_string (k) + tail); i >= 0) st.push_back ({ juce::String ("m" + std::to_string (k) + tail), p->param (i)->getDefaultValue() });
    return st;
}

bool Engine::transLoad (int slot, const juce::File& f)
{
    slot = juce::jlimit (0, 1, slot);
    auto smp = sf::loadSampleFile (f, sr, 120.0);
    if (smp == nullptr || smp->length() < 8) return false;
    transKept.push_back (smp);
    // (the audio thread takes a slot's file afresh each block: one replaced several loads ago is held by nothing but this list)
    while (transKept.size() > 6) transKept.erase (transKept.begin());
    (slot == 0 ? trans.leadSmp : trans.hitSmp) = smp.get();
    (slot == 0 ? trans.leadKind : trans.hitKind) = Transition::kFile;
    transFiles[(size_t) slot] = f.getFullPathName();
    return true;
}

void Engine::setFinish (float amount)
{
    for (auto& m : master)
        if (m->getName() == "SF FINISH")
            if (const int i = m->paramIndex ("amount"); i >= 0) m->param (i)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, amount));
}

SFProcessor* Engine::masterStage (const juce::String& module)
{
    for (auto& m : master) if (m->getName() == "SF " + module) return m.get();
    return nullptr;
}

const sf::FinishSays* Engine::finishSays() const
{
    for (auto& m : master)
        if (m->getName() == "SF FINISH") return dynamic_cast<const sf::FinishSays*> (&m->mod());
    return nullptr;
}

float Engine::finish() const
{
    for (auto& m : master)
        if (m->getName() == "SF FINISH")
            if (const int i = m->paramIndex ("amount"); i >= 0) return m->param (i)->getValue();
    return 0.0f;
}

const sf::SampleData* Engine::transAsset (const juce::String& path)
{
    if (path.isEmpty()) return nullptr;
    if (const auto it = transAssets.find (path); it != transAssets.end()) return it->second.get();
    auto smp = sf::loadSampleFile (juce::File (path), sr, 120.0);
    if (smp == nullptr || smp->length() < 8) return nullptr;
    transAssets[path] = smp;
    return smp.get();
}

double Engine::transFireTo (double atPpq, int leadKind, const sf::SampleData* lead, int hitKind, const sf::SampleData* hit, float leadGain, float hitGain)
{
    if (! running.load()) return -1.0;
    const double pos = songPos();
    if (atPpq <= pos) return -1.0;
    trans.askLeadKind = leadKind; trans.askLead = lead; trans.askHitKind = hitKind; trans.askHit = hit;
    trans.askLeadGain = leadGain; trans.askHitGain = hitGain;
    trans.askFrom = pos; trans.askAt = atPpq; trans.askNow = false;
    trans.askSeq.fetch_add (1);
    return atPpq;
}

bool Engine::setAt (int ctl, float v, double atPpq)
{
    if (ctl < 0 || ctl >= cx::kCount || ! running.load()) return false;
    for (auto& q : setsAt)
    {
        int free = 0;
        if (! q.state.compare_exchange_strong (free, 1)) continue;
        q.ctl = ctl; q.v = v; q.at = atPpq;
        q.state = 2;
        return true;
    }
    return false;
}

void Engine::clearSetsAt()
{
    for (auto& q : setsAt) { int waiting = 2; q.state.compare_exchange_strong (waiting, 0); }
}

double Engine::transFire (int bars)
{
    trans.askLeadKind = -1; trans.askHitKind = -1;         // (FIRE by hand: what the two slots say)
    trans.askLeadGain = 1.0f; trans.askHitGain = 1.0f;
    if (! running.load()) { trans.askNow = true; trans.askSeq.fetch_add (1); return -1.0; }
    const double len = (double) juce::jlimit (1, 64, bars) * 4.0, pos = songPos();
    const double at = std::ceil ((pos + 1.0) / len) * len;
    trans.askFrom = pos; trans.askAt = at; trans.askNow = false;
    trans.askSeq.fetch_add (1);
    return at;
}

Engine::SetupState Engine::sampleOwnOff (Target t) const
{
    SetupState st;
    if (auto* p = processorFor (t))
        for (const char* id : { "smp_gain", "smp_norm", "smp_play", "smp_env" })
            if (const int i = p->paramIndex (id); i >= 0 && i < p->numModuleParams())
                st.push_back ({ juce::String (id), juce::String (id) == "smp_env" ? 0.0f : p->param (i)->getDefaultValue() });     // (KNOBS: off, as samples played then; it opens on)
    return st;
}

Engine::SetupState Engine::newerOff (Target t) const
{
    SetupState st;
    if (auto* p = processorFor (t))
        for (const char* id : { "gr_pos", "gr_scan", "gr_size", "gr_dens", "gr_spray", "gr_pitch", "gr_shape", "gr_rev", "gr_clock", "gr_spread", "gr_follow", "gr_sync",
                                "smp_notes", "smp_loop", "smp_xfade", "smp_vel", "smp_rand", "smp_slice", "smp_cycle",
                                "fg_char", "fg_impact", "fg_body", "fg_ground", "fg_pos", "fg_tone", "fg_glen", "fg_low",
                                "fg_hang", "fg_curve", "fg_hold", "fg_hdec", "fg_phase", "fg_flip", "fg_ilen", "fg_ipitch", "fg_icut", "fg_iwidth",
                                "fg_vary", "fg_gdark", "fg_gduck", "fg_grec", "fg_gwidth" })
            if (const int i = p->paramIndex (id); i >= 0 && i < p->numModuleParams())
                st.push_back ({ juce::String (id), p->param (i)->getDefaultValue() });
    return st;
}

// LOOP on a drum's sample, and the PERC lane's sample (its START, END, fades, GAIN, NORMALISE, SLICE, CYCLE): a snapshot
// from before them does not name them, and is not left with what the one before it set.
float Engine::moduleSays (Target t, int what) const { auto* p = processorFor (t); return p != nullptr ? p->mod().meter (what) : 0.0f; }
bool Engine::isCompressor (Target t) const { auto* p = processorFor (t); return p != nullptr && p->getName() == "SF GLUE"; }
float Engine::plainPlace (Target t, const juce::String& id, float plain) const
{
    auto* p = processorFor (t);
    if (p == nullptr) return -1.0f;
    const int i = p->paramIndex (id.toStdString());
    return i >= 0 && i < p->numModuleParams() ? juce::jlimit (0.0f, 1.0f, p->param (i)->convertTo0to1 (plain)) : -1.0f;
}

bool Engine::setupPlays (Target t, const SetupState& st, const juce::String& model) const
{
    auto* p = processorFor (t);
    if (p == nullptr) return false;
    for (const char* id : { "model", "layer_model" })
    {
        const int i = p->paramIndex (id);
        if (i < 0) continue;
        const auto& ch = p->allSpecs()[(size_t) i].choices;
        if (ch.size() < 2) continue;
        float place = p->param (i)->getValue();
        for (auto& kv : st) if (kv.first == id) place = kv.second;
        const int at = juce::jlimit (0, (int) ch.size() - 1, (int) std::lround (place * (float) (ch.size() - 1)));
        if (model == juce::String (ch[(size_t) at])) return true;
    }
    return false;
}

Engine::SetupState Engine::laterOff (Target t) const
{
    SetupState st;
    if (t.kind != 0)
    {
        // (an effect's, a return's: what a reverb's DUCK makes room for, the kick and the source as it always was)
        if (auto* p = processorFor (t)) if (const int i = p->paramIndex ("duck_for"); i >= 0 && i < p->numModuleParams()) st.push_back ({ "duck_for", 0.0f });
        // (a compressor's, since the ninetieth build: its KNEE, AUTO RELEASE and START FROM as they open. A snapshot from
        // before names none of them, and must not be left with the last compressor's.)
        if (auto* p = processorFor (t); p != nullptr && p->getName() == "SF GLUE")
            for (const char* id : { "knee", "auto", "start" }) if (const int i = p->paramIndex (id); i >= 0 && i < p->numModuleParams()) st.push_back ({ juce::String (id), p->param (i)->getDefaultValue() });
        return st;
    }
    // (SF-000, the hundred and third build) A lane: everything it has that is not on its panel's last four (which the
    // snapshot's values hold) nor which engine plays (fromListsBefore), as it opens. A snapshot of now names all of it;
    // one from before the lane was one instrument names some of it, and the rest is as a new lane has it.
    if (auto* p = processorFor (t))
    {
        const auto& d = lanes()[(size_t) t.lane];
        const auto& specs = p->allSpecs();
        for (int i = 0; i < p->numModuleParams(); ++i)
        {
            const juce::String id (specs[(size_t) i].id);
            bool panel = id == "model" || id == "layer_model";
            for (int k = kSoundKnobs; k < kLaneKnobs; ++k) panel = panel || id == d.knobs[k].param;
            if (! panel) st.push_back ({ id, p->param (i)->getDefaultValue() });
        }
    }
    return st;
}

bool Engine::lanePlaysNotes (int l) const
{
    if (l < 0 || l >= kLanes) return false;
    if (auto* v = lane[(size_t) l].voice.load(); v != nullptr && v->plays != nullptr) return v->plays->load() >= 0.5f;
    return lanes()[(size_t) l].pitched;
}

// (the hundred and twelfth build) the ninth knob is one control on every lane, the lane's DECAY or GATE: named for what PLAYS
// makes it, so the panel, INFO and the controllers say what it does now
void Engine::playsMoved (int l)
{
    if (l < 0 || l >= kLanes) return;
    auto& c = controls[(size_t) cx::lane (l, kSoundKnobs)];
    const juce::String name = lanePlaysNotes (l) ? "GATE" : "DECAY";
    if (c.label != name) { c.label = name; stateMoved(); }
}

bool Engine::laneFollowsKey (int lane) const
{
    if (auto* p = processorFor ({ 0, lane, 0 }))
        if (const int i = p->paramIndex ("key_follow"); i >= 0 && i < p->numModuleParams())
            return p->param (i)->getValue() >= 0.5f;
    return false;
}

void Engine::setSetup (Target t, const juce::String& id, float v)
{
    stateMoved();
    if (t.kind == 2 && t.slot == 0 && id == kToSpaceId) { dubToSpace = juce::jlimit (0.0f, 1.0f, v); return; }
    if (t.kind == kMixPage)
    {
        // the mix: a fader, or a band of the master's EQ
        if (const int k = mixOfId (id); k >= 0) { setMixFader (k, v); return; }
        if (int b = 0, w = 0; eqId (id, b, w))
        {
            auto band = eqBand (kLanes, b);
            if (w == 0) band.type = (int) std::lround (juce::jlimit (0.0f, 1.0f, v) * (float) (kEqTypes - 1));
            else if (w == 1) band.hz = eqHzOf (v);
            else if (w == 2) band.db = eqDbOf (v);
            else band.q = eqQOf (v);
            setEqBand (kLanes, b, band);
        }
        return;
    }
    if (int b = 0, w = 0; t.kind == 0 && eqId (id, b, w))
    {
        // a band of the lane's EQ (a knob band's gain is its knob's: the session sets that)
        auto band = eqBand (t.lane, b);
        if (w == 0) band.type = (int) std::lround (juce::jlimit (0.0f, 1.0f, v) * (float) (kEqTypes - 1));
        else if (w == 1) band.hz = eqHzOf (v);
        else if (w == 2) band.db = eqDbOf (v);
        else band.q = eqQOf (v);
        setEqBand (t.lane, b, band);
        return;
    }
    if (id == kModelId)
    {
        if (t.kind == 2 && t.slot == 1) setSpaceModel ((int) std::lround (juce::jlimit (0.0f, 1.0f, v) * (float) (kSpaceModels - 1)));
        return;
    }
    if (auto* p = processorFor (t))
    {
        const int i = p->paramIndex (id.toStdString());
        const bool motion = isMotionId (id);
        if (i < 0 || (i >= p->numModuleParams() && ! motion)) return;
        v = juce::jlimit (0.0f, 1.0f, v);
        if (motion && id.endsWith ("_target"))
        {
            // (CONFIG lists what it moves in its own order, and only what the page shows: back to the instrument's)
            const int now = (int) std::lround (p->param (i)->convertFrom0to1 (p->param (i)->getValue()));
            const auto order = motionOrder (t, *p, shownParams (*p, boundIds (t), setupParams (t)), now);
            const int at = juce::jlimit (0, (int) order.size(), (int) std::lround (v * (float) order.size()));
            v = p->param (i)->convertTo0to1 (at == 0 ? 0.0f : (float) order[(size_t) at - 1]);
        }
        p->param (i)->setValueNotifyingHost (v);
        if (t.kind == 0 && id == "plays") playsMoved (t.lane);
        // (the hundred and fifteenth build) heard from the next note (the chord, the voices, PLAYS turned elsewhere): a note
        // held on through it, a drone above all, is struck again, or it would never be heard
        if (t.kind == 0 && id != "model" && id != "layer_model" && id != "plays" && reachOf (t.lane, id) == sf::Reach::Next
            && (dynamic_cast<juce::AudioParameterChoice*> (p->param (i)) != nullptr || dynamic_cast<juce::AudioParameterBool*> (p->param (i)) != nullptr))      // (a menu or a switch: a value dragged along would strike it over and over)
            lane[(size_t) t.lane].restrike = true;
        if (t.kind == 0 && id == "model" && remapLane (t.lane, true))    // (a model chosen by hand: its own knobs, where they start)
            if (const int now = mapOf (t.lane, *p); now > 0)
                for (auto& w : lanes()[(size_t) t.lane].own[(size_t) now - 1].with)       // (and what it puts away)
                    if (const int wi = p->paramIndex (w.param); wi >= 0 && wi < p->numModuleParams())
                    {
                        p->param (wi)->setValueNotifyingHost (w.place);
                        // (one of its own sound knobs: the knob stands there too, or it would put the parameter back)
                        if (auto* vc = lane[(size_t) t.lane].voice.load())
                            for (int k = 0; k < kSoundKnobs; ++k)
                                if (const auto& b = vc->maps[(size_t) now][(size_t) k]; b.pi == wi && b.n == 0)
                                    set (cx::lane (t.lane, k), juce::jlimit (0.0f, 1.0f, (w.place - b.lo) / std::max (1e-6f, b.hi - b.lo)));
                    }
        // (SF-000, the hundred and second build) SYNTH chosen by hand, as the model or the layer, opens on the lane's own
        // patch (BASS LADDER on BASS, HAT 909 on CLOSED HAT: SynthEngine's lanePatch), unless the other already plays it;
        // and PATCH chosen by hand puts every control where that patch has it.
        if (t.kind == 0 && (id == "model" || id == "layer_model"))
        {
            const auto text = [&] (const char* pid) { const int k = p->paramIndex (pid); return k >= 0 ? p->param (k)->getCurrentValueAsText().toUpperCase() : juce::String(); };
            const juce::String other = id == "model" ? text ("layer_model") : text ("model");
            if (text (id.toRawUTF8()) == "SYNTH" && other != "SYNTH")
                synthPatch (t.lane, *p, sf::syn::patchIndex (sf::syn::lanePatch (lanes()[(size_t) t.lane].slug)));
        }
        if (t.kind == 0 && id == "sy_patch") synthPatch (t.lane, *p, (int) std::lround (p->param (i)->convertFrom0to1 (v)));
        updateLatency();
    }
}

Engine::SetupState Engine::getSetup (Target t) const
{
    SetupState st;
    if (t.kind == kMixPage)
    {
        // the mix: every fader, and the master's EQ where it is not as it starts
        for (int k = 0; k < kMixFaders; ++k) st.push_back ({ mixId (k), mixFader (k) });
        if (! eqIsDefault (kLanes))
            for (int b = 0; b < kEqBands; ++b)
            {
                const auto band = eqBand (kLanes, b);
                st.push_back ({ eqIdOf (b, 0), (float) band.type / (float) (kEqTypes - 1) });
                st.push_back ({ eqIdOf (b, 1), eqHzPlace (band.hz) });
                st.push_back ({ eqIdOf (b, 2), eqDbPlace (band.db) });
                st.push_back ({ eqIdOf (b, 3), eqQPlace (band.q) });
            }
        return st;
    }
    if (auto* p = processorFor (t))
    {
        // SPACE's setup says which reverb it was read from: it is only ever put back on that one
        if (t.kind == 2 && t.slot == 1) st.push_back ({ kModelId, (float) spaceWant.load() });
        // (DUB's TO SPACE, the engine's own, with DUB's setup: it is not SPACE's, so a snapshot from another reverb keeps it)
        if (t.kind == 2 && t.slot == 0) st.push_back ({ kToSpaceId, dubToSpace.load() });
        const auto bound = boundIds (t);
        const auto& specs = p->allSpecs();
        for (int i = 0; i < p->numModuleParams(); ++i)
            if (std::find (bound.begin(), bound.end(), specs[(size_t) i].id) == bound.end())
            {
                // (a slice marker not in use is not written: a snapshot that does not name one has none there, laterOff)
                if (specs[(size_t) i].hidden && p->param (i)->getValue() == 0.0f && juce::String (specs[(size_t) i].id).startsWith ("smp_cut")) continue;
                st.push_back ({ specs[(size_t) i].id, p->param (i)->getValue() });
            }
        // its MOTION with it
        {
            for (int i = p->numModuleParams(); i < (int) specs.size(); ++i)
                if (isMotionId (specs[(size_t) i].id)) st.push_back ({ specs[(size_t) i].id, p->param (i)->getValue() });
        }
    }
    // a lane's EQ with it, where it is not as it starts (the LOW, MID and HIGH knobs' gains are controls of their own)
    if (t.kind == 0 && ! eqIsDefault (t.lane))
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto band = eqBand (t.lane, b);
            st.push_back ({ eqIdOf (b, 0), (float) band.type / (float) (kEqTypes - 1) });
            st.push_back ({ eqIdOf (b, 1), eqHzPlace (band.hz) });
            if (eqKnobOf (b) < 0) st.push_back ({ eqIdOf (b, 2), eqDbPlace (band.db) });
            st.push_back ({ eqIdOf (b, 3), eqQPlace (band.q) });
        }
    return st;
}

// A menu's place, kept when its list was one shorter (the newcomer second to last, so the first and the last have not
// moved): it falls on the shorter list's steps, which but for the two ends are never the longer one's.
static float grownPlace (const SFProcessor& p, const juce::String& id, float v)
{
    if (id != "model" && id != "layer_model") return v;
    const juce::String name = p.getName();
    const int now = name == "SF BASS" ? 10 : name == "SF LEAD" ? 12 : 0;
    if (now == 0) return v;
    const int i = p.paramIndex (id.toStdString());
    if (i < 0 || (int) p.allSpecs()[(size_t) i].choices.size() != now) return v;
    const float was = v * (float) (now - 2);                            // (its place on the list of before)
    const int k = (int) std::lround (was);
    if (std::abs (was - (float) k) > 0.02f || k <= 0 || k >= now - 2) return v;
    return (float) k / (float) (now - 1);
}

Engine::SetupState Engine::grown (Target t, SetupState st) const
{
    if (auto* p = processorFor (t)) for (auto& kv : st) kv.second = grownPlace (*p, kv.first, kv.second);
    return st;
}

void Engine::applySetupTo (SFProcessor& p, const std::vector<std::string>& bound, const SetupState& stIn)
{
    SetupState st = stIn;
    for (auto& kv : st) kv.second = grownPlace (p, kv.first, kv.second);
    for (auto& kv : st)
    {
        const int i = p.paramIndex (kv.first.toStdString());
        if (i < 0 || (i >= p.numModuleParams() && ! isMotionId (kv.first)) || std::find (bound.begin(), bound.end(), kv.first.toStdString()) != bound.end()) continue;
        auto* prm = p.param (i);
        if (std::abs (prm->getValue() - kv.second) > 1e-6f) prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, kv.second));
    }
}

void Engine::applySetup (Target t, const SetupState& st)
{
    if (t.kind == kMixPage)
    {
        // the mix as a set has it: a fader it does not name is as it comes, and so is the master's EQ if it names no band
        for (int k = 0; k < kMixFaders; ++k) mixPlace[(size_t) k] = kMixUnity;
        eqReset (kLanes);
        for (auto& kv : st) setSetup (t, kv.first, kv.second);
        return;
    }
    if (t.kind == 2 && t.slot == 0)
    {
        // DUB's TO SPACE: as the setup has it, and none where it says nothing of it (from before there was one)
        float to = 0.0f;
        for (auto& kv : st) if (kv.first == kToSpaceId) to = kv.second;
        dubToSpace = juce::jlimit (0.0f, 1.0f, to);
    }
    if (auto* p = processorFor (t))
    {
        if (t.kind == 2 && t.slot == 1)
        {
            // Which reverb SPACE is belongs to the rig, not to a snapshot: setup from the other one is left out.
            int from = 0;
            for (auto& kv : st) if (kv.first == kModelId) from = (int) std::lround (kv.second);
            if (from != spaceWant.load()) return;
        }
        if (t.kind == 0 && ! lanes()[(size_t) t.lane].own.empty())
        {
            // the model first: which controls are knobs and which are CONFIG follows from it
            for (auto& kv : st) if (kv.first == "model") { applySetupTo (*p, {}, { kv }); break; }
            remapLane (t.lane, false);
        }
        applySetupTo (*p, boundIds (t), st);
        if (t.kind == 0)
        {
            // (a setup from before PLAYS, the hundred and twelfth build: the lane plays as it always did)
            bool named = false;
            for (auto& kv : st) named = named || kv.first == "plays";
            if (! named && st.size() > 1)          // (a whole setup: the model alone, set ahead of the rest, says nothing of it)
                if (const int i = p->paramIndex ("plays"); i >= 0 && i < p->numModuleParams())
                    p->param (i)->setValueNotifyingHost (p->param (i)->getDefaultValue());
            playsMoved (t.lane);
        }
        updateLatency();
    }
    if (t.kind == 0)
    {
        // the lane's EQ: as the setup has it, and as it starts where a lane's whole setup says nothing of it (a snapshot
        // from before the EQ, or one whose EQ was untouched)
        bool any = false;
        for (auto& kv : st) { int b = 0, w = 0; any = any || eqId (kv.first, b, w); }
        if (any) { for (auto& kv : st) { int b = 0, w = 0; if (eqId (kv.first, b, w)) setSetup (t, kv.first, kv.second); } }
        else if (st.size() > 6) eqReset (t.lane);
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
    // MOTION's waves: RANDOM, DRIFT and STEPS are the same every time, each unit its own
    for (int u = 0; u < cx::kMotUnits; ++u) motState->wave[(size_t) u].prepare (sr, 0x5F0A11C3u + 0x9E3779B9u * (uint32_t) u);
    motPpq = 0.0;
    // nothing is part-way between two effects or two instruments when the audio starts: what was leaving is let go
    collectRetired (false);
    for (auto& ln : lane)
        for (int k = 0; k < kStages; ++k)
        {
            ln.fadeSeen[k] = ln.fadeSeq[k].load(); ln.fadeDone[k] = ln.fadeSeen[k];
            ln.fading[k] = false; ln.fadeG[k] = 1.0f; ln.fadeAt[k] = 1.0f;
        }
    for (int l = 0; l < kLanes; ++l) { dropHold (l); lane[(size_t) l].holdingNow = false; }      // (and nothing is held between two)
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
    prep (*busProc);
    for (int l = 0; l < kLanes; ++l) busWas[(size_t) l] = laneOnBus (l) ? 1.0f : 0.0f;
    transBuf.setSize (2, maxBlock, false, true, true); trans.prepare (sr);
    for (auto* b : { &buf, &mix, &sendA, &sendB, &work, &pre, &ghost, &recBuf, &cueBuf, &busBuf, &busDry }) b->setSize (2, maxBlock, false, true, false);
    laneBuf.setSize (2 * kLanes, maxBlock, false, true, false);
    pockets.prepare (sr, maxBlock);
    cueBuf.clear(); cueNow = nullptr; cueGain = 0.0f; cuePos = 0.0; splitGain = 0.0f;
    cueRigGain = 0.0f; cueRigPlaying = 0; cueRigOn = false; cueRigMissed = 0;
    kTape = std::max (1 << 20, juce::nextPowerOfTwo ((int) std::ceil (sr * 16.0) + 65536 + maxBlock));
    for (auto& t : tape) t.assign ((size_t) kTape, 0.0f);
    stemTape.resize ((size_t) kStems);
    for (auto& st : stemTape) for (auto& t : st) t.assign ((size_t) kTape, 0.0f);
    tapePos = 0; tapeEnd = 0; markCount = 0; runCount = 0;
    liveMidi.ensureSize (8192);
    wallValid = false; wallMs = -1.0; noteKeptN = 0; clkPpq = 0.0; clkWas = false;
    ghostMidi.ensureSize (4096);            // (a voice leaving is given its lane's notes: room for them before the audio starts)
    fol = Follow();
    loud->m.prepare (sr);
    for (auto& m : loud->lane) m.prepare (sr);
    for (auto& p : loud->post) { p.prepare (sr, 2048); p.dropDuck = false; }        // (the drop's duck is the instrument's own pocket's still)
    loud->probe.prepare (sr, 2048); loud->probe.dropDuck = false;
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

// MOTION on the snapshot knobs, for this block: where each wave is at the block's end, how far that
// swings its knob, and what that does to every control the knob moves (mOff).
void Engine::knobMotion (double beats, int n)
{
    // Where the waves are in the song: the clock's own place while it runs, so every wave falls on
    // the bar the same way each time; stopped, they carry on from where they were.
    motPpq = wasRunning ? ppq + beats : motPpq + beats;
    std::array<float, 16> swing {};
    bool any = false;
    sf::Ctx c;
    c.sr = sr; c.n = n; c.bpm = blockBpm; c.ppq = motPpq - beats; c.playing = wasRunning; c.beatsPerBar = 4.0; c.bus = &sf::Bus::get();
    for (int u = 0; u < cx::kMotUnits; ++u)
    {
        const int w = juce::jlimit (0, kWaves - 1, (int) std::lround (cur[(size_t) cx::mot (u, 0)] * (float) (kWaves - 1)));
        const float amount = (cur[(size_t) cx::mot (u, 3)] - 0.5f) * 2.0f;
        if (w == 0 || std::abs (amount) < 1.0e-4f) { motState->wave[(size_t) u].reset(); continue; }
        const int rate = (int) std::lround (cur[(size_t) cx::mot (u, 1)] * (float) (sf::kMotionRates - 1));
        const int k = juce::jlimit (0, 15, (int) std::lround (cur[(size_t) cx::mot (u, 2)] * 15.0f));
        swing[(size_t) k] += amount * motState->wave[(size_t) u].tick (kWaveSrc[w], rate, c, motPpq, n, 0.0f);
        any = true;
    }
    if (! any && ! mOn) return;
    mOff.fill (0.0f);
    mOn = any;
    if (! any) return;
    const KnobLaws* laws = knobLaws.load();
    for (int k = 0; k < 16; ++k)
    {
        if (swing[(size_t) k] == 0.0f) continue;
        // the knob swung, as far as its own travel lets it; and everything it moves, by its own law
        const float held = laws != nullptr ? laws->knob[(size_t) k].centre : -1.0f;
        const float v = held >= 0.0f ? held : cur[(size_t) (cx::kSnap + k)], v2 = juce::jlimit (0.0f, 1.0f, v + swing[(size_t) k]);
        if (v2 == v) continue;
        if (held < 0.0f) mOff[(size_t) (cx::kSnap + k)] = v2 - v;        // (the knob is seen to swing, unless it is being used to choose)
        if (laws == nullptr) continue;
        const auto& law = laws->knob[(size_t) k];
        for (const auto& r : law.rows) mOff[(size_t) r.ctl] += law.at (r, v2) - law.at (r, v);
    }
}

// What the lanes' locked steps add to their knobs in this block (stepPush), read at the block's end: a step's lock is in
// place by the block its note strikes in, never after it.
// A locked step's differences come on at its start and hold for its gate (a step that is not lit: for the step), then
// ease back over 15 ms. Where a note is sounding into the step (the gate before it reaches it: a slide, a tie, a held
// note), they are slid to over the first half of the step and not jumped to; and a lit step with no lock of its own,
// reached that way, slides them back to nought.
void Engine::stepLocks (double beats)
{
    lkOn = false;
    const bool run = wasRunning && beats > 1.0e-9;
    const double q0 = ppq, q1 = ppq + beats;
    for (int l = 0; l < kLanes; ++l)
    {
        auto& lp = lockPlay[(size_t) l];
        const Pattern& pat = lane[(size_t) l].player.pat;
        const int len = juce::jlimit (1, kMaxSteps, pat.length);
        bool has = false;
        if (run && cur[(size_t) cx::lane (l, cx::kSeqMute)] < 0.5f)
            for (auto& k : pat.locks) if (k.step < len && pat.steps[(size_t) k.step].lock) { has = true; break; }
        // (stopped, muted, or the song somewhere else than the last block left it: nothing is held over)
        if (! run || std::abs (q0 - lp.lastEnd) > 1.0e-6 || (! has && ! lp.busy))
        {
            if (lp.busy) { lp.now.fill (0.0f); for (int c = 0; c < cx::kGen; ++c) stepPush[(size_t) cx::lane (l, c)] = 0.0f; lp.busy = false; }
            lp.up = false; lp.len = 0.0; lp.from.fill (0.0f); lp.to.fill (0.0f);
            lp.lastEnd = run ? q1 : -1.0;
            if (! has) continue;
        }
        lp.lastEnd = q1;
        auto at = [&lp] (double q, std::array<float, cx::kGen>& out)
        {
            const float t = lp.len > 1.0e-9 ? (float) juce::jlimit (0.0, 1.0, (q - lp.start) / lp.len) : 1.0f;
            for (int c = 0; c < cx::kGen; ++c) out[(size_t) c] = lp.from[(size_t) c] + (lp.to[(size_t) c] - lp.from[(size_t) c]) * t;
        };
        auto go = [&] (const std::array<float, cx::kGen>& to, double from, double over) { std::array<float, cx::kGen> here; at (from, here); lp.from = here; lp.to = to; lp.start = from; lp.len = over; };
        const auto first = (int64_t) std::ceil (q0 / kStepBeats - 1e-9);
        for (int64_t k = first; (double) k * kStepBeats < q1 - 1e-12; ++k)
        {
            if (k < 0) continue;
            const double tq = (double) k * kStepBeats;
            const int i = (int) (k % len);
            const Step& st = pat.steps[(size_t) i];
            std::array<float, cx::kGen> d {};
            bool locked = false;
            if (st.lock) for (auto& e : pat.locks) if (e.step == i && e.ctl < cx::kGen && controls[(size_t) cx::lane (l, e.ctl)].glides) { d[(size_t) e.ctl] = e.off; locked = true; }      // (a knob that travels: never a menu or a switch)
            if (! locked && ! (st.on && lp.up)) continue;
            // is a note sounding into this step?
            bool into = false;
            for (int back = 1; back < len; ++back)
                if (const Step& b = pat.steps[(size_t) (((i - back) % len + len) % len)]; b.on)
                { into = b.ratchet == 1 && b.gate >= (float) back + (st.on ? kLegatoOver : 0.03f); break; }
            go (d, tq, into ? kStepBeats * 0.5 : 0.0);        // (slid to over the first half of the step, and there for the rest of it)
            lp.up = locked;
            if (locked)
            {
                const int dist = stepsToNext (pat, i);
                const double hold = ! st.on || st.ratchet > 1 ? 1.0 : st.gate >= (float) dist + kLegatoOver ? (double) dist : juce::jlimit (0.05, (double) dist, (double) st.gate);
                lp.holdEnd = tq + hold * kStepBeats;
            }
        }
        if (lp.up && q1 > lp.holdEnd + 1.0e-9) { go (std::array<float, cx::kGen> {}, std::max (q0, lp.holdEnd), 0.015 * blockBpm / 60.0); lp.up = false; }
        at (q1, lp.now);
        bool any = lp.up;
        for (int c = 0; c < cx::kGen; ++c) { stepPush[(size_t) cx::lane (l, c)] = lp.now[(size_t) c]; any = any || std::abs (lp.now[(size_t) c]) > 1.0e-6f; }
        lp.busy = any;
        lkOn = lkOn || any;
    }
}

void Engine::advanceControls (double beats, int n)
{
    gClock += beats;
    glidingNow = false;
    knobMotion (beats, n);
    stepLocks (beats);
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
            else if (len > 0.0f && wasRunning)
            {
                // A switch or a list that cannot travel, sent with a recall whose knobs do: it goes
                // over on the beat they set off on, all at once, and not before.
                gStart[s] = gClock - beats + (std::ceil (ppq - 1e-9) - ppq);
                gLen[s] = 1.0e-9; gliding[s] = true;
            }
            else if (len < 0.0f && wasRunning)
            {
                // The same, sent to go with a recall's notes: over on the first bar line at or after the end of a travel
                // that long (as a waiting pattern is taken up: setPattern's afterBeats).
                const double due = std::ceil ((std::ceil (ppq - 1e-9) + (double) -len) / 4.0 - 1e-6) * 4.0;
                gStart[s] = gClock - beats + (due - ppq);
                gLen[s] = 1.0e-9; gliding[s] = true;
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
        if (cur[s] != curSaid[s]) { curSaid[s] = cur[s]; baseV[s] = cur[s]; }
        // what sounds: where it stands, MOTION's push on it, and a locked step's
        const float out = mOn || lkOn ? juce::jlimit (0.0f, 1.0f, cur[s] + (mOn ? mOff[s] : 0.0f) + (lkOn ? stepPush[s] : 0.0f)) : cur[s];
        snd[s] = out;
        if (out != applied[s])
        {
            applied[s] = out;
            shownV[s] = out;
            if (bind[s].raw != nullptr) applyBind (bind[s], out);
            else if (i < cx::kRet)
            {
                const int k = i % cx::kPerLane;
                if (k < kLaneKnobs)
                {
                    auto& ln = lane[(size_t) (i / cx::kPerLane)];
                    auto* v = ln.voice.load();
                    if (v != nullptr) applyBind (v->bind (k), out);
                    // one on its way out follows the knobs that travel; a menu or a switch stays as it left it, and so
                    // does a place that means something else on it (another model, with knobs of its own)
                    if (controls[s].glides && ln.fading[kSlots]) if (auto* o = ln.voiceOut.load()) if (v == nullptr || o->bind (k).pi == v->bind (k).pi) applyBind (o->bind (k), out);
                }
                else if (k >= cx::kIns && k < cx::kGen)
                    if (auto* in = lane[(size_t) (i / cx::kPerLane)].ins[(k - cx::kIns) / kCells].load())
                        if (! (in->soloOn && (k - cx::kIns) % kCells == in->mixCell))
                            applyBind (in->bind[(k - cx::kIns) % kCells], out);
            }
        }
    }
}

const char* Engine::eqTypeName (int type)
{
    static const char* const n[kEqTypes] = { "OFF", "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH" };
    return n[juce::jlimit (0, (int) kEqTypes - 1, type)];
}

Engine::EqBand Engine::eqDefault (int band)
{
    // LOW: a shelf below 200 Hz. MID: a wide bell at 700 Hz. HIGH: a shelf above 2.5 kHz (the three-knob EQ, as it always
    // was). The five between and after them: off, a bell waiting at a place of its own.
    static const EqBand d[kEqBands] = { { eqLowShelf, 200.0f, 0.0f, 0.70710678f }, { eqOff, 80.0f, 0.0f, 0.70710678f }, { eqOff, 350.0f, 0.0f, 1.0f },
                                        { eqBell, 700.0f, 0.0f, 0.6f }, { eqOff, 1400.0f, 0.0f, 1.0f }, { eqOff, 5000.0f, 0.0f, 1.0f },
                                        { eqHighShelf, 2500.0f, 0.0f, 0.70710678f }, { eqOff, 12000.0f, 0.0f, 0.70710678f } };
    return d[juce::jlimit (0, kEqBands - 1, band)];
}

bool Engine::eqId (const juce::String& id, int& band, int& what)
{
    if (id.length() < 5 || ! id.startsWith ("eq") || id[3] != '_') return false;
    band = id[2] - '1';
    if (band < 0 || band >= kEqBands) return false;
    const auto tail = id.substring (4);
    what = tail == "type" ? 0 : tail == "hz" ? 1 : tail == "gain" ? 2 : tail == "q" ? 3 : -1;
    return what >= 0;
}

juce::String Engine::eqIdOf (int band, int what)
{
    static const char* const t[4] = { "type", "hz", "gain", "q" };
    return "eq" + juce::String (band + 1) + "_" + t[juce::jlimit (0, 3, what)];
}

// A band's five coefficients (b0 b1 b2 a1 a2, a0 divided out): the cookbook's filters.
void Engine::eqCoefs (int type, double hz, double db, double q, double sr, double c[5])
{
    hz = juce::jlimit (10.0, 0.45 * sr, hz);
    // (the three knobs' own widths, to the last bit what they always were)
    const double qd = std::abs (q - 0.70710678) < 1.0e-6 ? 1.0 / std::sqrt (2.0) : std::abs (q - 0.6) < 1.0e-6 ? 0.6 : juce::jlimit (0.1, 40.0, q);
    const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * hz / sr, cs = std::cos (w), sn = std::sin (w);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    if (type == eqBell)
    {
        const double al = sn / (2.0 * qd);
        b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A;
    }
    else if (type == eqLowShelf || type == eqHighShelf)
    {
        const double al = qd == 1.0 / std::sqrt (2.0) ? sn / 2.0 * std::sqrt (2.0) : sn / (2.0 * qd), rt = 2.0 * std::sqrt (A) * al, sg = type == eqLowShelf ? 1.0 : -1.0;
        b0 = A * ((A + 1) - sg * (A - 1) * cs + rt);
        b1 = sg * 2 * A * ((A - 1) - sg * (A + 1) * cs);
        b2 = A * ((A + 1) - sg * (A - 1) * cs - rt);
        a0 = (A + 1) + sg * (A - 1) * cs + rt;
        a1 = -sg * 2 * ((A - 1) + sg * (A + 1) * cs);
        a2 = (A + 1) + sg * (A - 1) * cs - rt;
    }
    else if (type == eqLowCut || type == eqHighCut)
    {
        const double al = sn / (2.0 * qd), k = type == eqLowCut ? 1.0 + cs : 1.0 - cs;
        b0 = k / 2; b1 = type == eqLowCut ? -k : k; b2 = k / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al;
    }
    else if (type == eqNotch)
    {
        const double al = sn / (2.0 * qd);
        b0 = 1; b1 = -2 * cs; b2 = 1; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al;
    }
    c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
}

void Engine::setEq (EqStrip& ln, int band, int type, float hz, float db, float q)
{
    double c[5];
    eqCoefs (type, hz, db, q, sr, c);
    auto& e = ln.eq[band];
    e.b0 = (float) c[0]; e.b1 = (float) c[1]; e.b2 = (float) c[2]; e.a1 = (float) c[3]; e.a2 = (float) c[4];
}

Engine::EqBand Engine::eqBand (int l, int band) const
{
    EqBand b;
    if (l < 0 || l > kLanes || band < 0 || band >= kEqBands) return b;           // (kLanes: the master's)
    const auto& ln = eqOf (l);
    b.type = ln.eqType[band].load(); b.hz = ln.eqHz[band].load(); b.q = ln.eqQ[band].load();
    const int knob = l < kLanes ? eqKnobOf (band) : -1;         // (the master's has no knobs on a panel: all eight are its own)
    b.db = knob >= 0 ? eqKnobDb (target (cx::lane (l, cx::kEqLow + knob))) : ln.eqGain[band].load();
    return b;
}

void Engine::setEqBand (int l, int band, EqBand b)
{
    if (l < 0 || l > kLanes || band < 0 || band >= kEqBands) return;
    auto& ln = eqOf (l);
    ln.eqType[band] = juce::jlimit (0, (int) kEqTypes - 1, b.type);
    ln.eqHz[band] = juce::jlimit (20.0f, 20000.0f, b.hz);
    ln.eqQ[band] = juce::jlimit (0.3f, 12.0f, b.q);
    if (l >= kLanes || eqKnobOf (band) < 0) ln.eqGain[band] = juce::jlimit (-18.0f, 18.0f, b.db);
    stateMoved();
}

bool Engine::eqIsDefault (int l) const
{
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto now = eqBand (l, b), d = eqDefault (b);
        if (now.type != d.type || std::abs (now.hz - d.hz) > 0.01f * d.hz || std::abs (now.q - d.q) > 1.0e-3f || ((l >= kLanes || eqKnobOf (b) < 0) && std::abs (now.db) > 1.0e-3f)) return false;
    }
    return true;
}

void Engine::eqReset (int l)
{
    for (int b = 0; b < kEqBands; ++b) setEqBand (l, b, eqDefault (b));
}

float Engine::eqResponse (int l, float hz, int only) const
{
    double db = 0.0;
    const double w = juce::MathConstants<double>::twoPi * (double) hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    for (int b = 0; b < kEqBands; ++b)
    {
        if (only >= 0 && b != only) continue;
        const auto band = eqBand (l, b);
        if (band.type == eqOff || (eqHasGain (band.type) && std::abs (band.db) < 0.005f)) continue;
        double c[5];
        eqCoefs (band.type, band.hz, band.db, band.q, sr, c);
        const double m = std::abs ((c[0] + c[1] * z1 + c[2] * z2) / (1.0 + c[3] * z1 + c[4] * z2));
        db += 20.0 * std::log10 (std::max (1.0e-6, m));
    }
    return (float) db;
}

// An EQ knob: centre is flat, down cuts to -24 dB, up lifts to +9 dB.
static float eqDbFor (float v) { return v < 0.5f ? -24.0f * (1.0f - 2.0f * v) : 9.0f * (2.0f * v - 1.0f); }

// An EQ's eight bands brought to what is wanted (audio thread): each band's gain, place and width travel to it, and its
// filter is made again where any of them has moved. knobLane: the lane whose LOW, MID and HIGH knobs three of the bands'
// gains are (-1: the master's, which has none). Is any band in?
bool Engine::eqUpdate (EqStrip& ln, int knobLane, const float* snd)
{
    bool eqOn = false;
    for (int e = 0; e < kEqBands; ++e)
    {
        const int knob = knobLane >= 0 ? eqKnobOf (e) : -1, type = ln.eqType[e].load (std::memory_order_relaxed);
        const float want = knob >= 0 ? eqDbFor (snd[(size_t) cx::lane (knobLane, cx::kEqLow + knob)]) : ln.eqGain[e].load (std::memory_order_relaxed);
        const float wantHz = ln.eqHz[e].load (std::memory_order_relaxed), wantQ = ln.eqQ[e].load (std::memory_order_relaxed);
        ln.eqDb[e] += 0.25f * (want - ln.eqDb[e]);
        if (std::abs (want - ln.eqDb[e]) < 0.02f) ln.eqDb[e] = want;
        // (a band's place and width travel as its gain does: no step in the sound as it is dragged)
        if (ln.eqHzNow[e] != wantHz) { ln.eqHzNow[e] = ln.eqHzNow[e] < 1.0f ? wantHz : ln.eqHzNow[e] * std::pow (wantHz / ln.eqHzNow[e], 0.25f); if (std::abs (wantHz - ln.eqHzNow[e]) < 0.001f * wantHz) ln.eqHzNow[e] = wantHz; }
        if (ln.eqQNow[e] != wantQ) { ln.eqQNow[e] = ln.eqQNow[e] <= 0.0f ? wantQ : ln.eqQNow[e] + 0.25f * (wantQ - ln.eqQNow[e]); if (std::abs (wantQ - ln.eqQNow[e]) < 0.001f) ln.eqQNow[e] = wantQ; }
        if (type != ln.eqTypeSet[e] || std::abs (ln.eqDb[e] - ln.eqSet[e]) > 0.01f || ln.eqHzNow[e] != ln.eqHzSet[e] || ln.eqQNow[e] != ln.eqQSet[e])
        {
            setEq (ln, e, type, ln.eqHzNow[e], ln.eqDb[e], ln.eqQNow[e]);
            ln.eqTypeSet[e] = type; ln.eqSet[e] = ln.eqDb[e]; ln.eqHzSet[e] = ln.eqHzNow[e]; ln.eqQSet[e] = ln.eqQNow[e];
        }
        const bool live = type != eqOff && (! eqHasGain (type) || std::abs (ln.eqDb[e]) > 0.01f || std::abs (want) > 0.01f);
        if (live && ! ln.eqLive[e]) { ln.eq[e].z1[0] = ln.eq[e].z1[1] = ln.eq[e].z2[0] = ln.eq[e].z2[1] = 0.0f; }
        ln.eqLive[e] = live;
        eqOn = eqOn || live;
    }
    return eqOn;
}


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
    const bool recMore = rec != nullptr && rec->wantsMore();
    if (rec != nullptr) rec->begin (n);
    {
        // the stems' tapes: silence for this buffer, until each stem that sounds is written over it
        const int at = (int) (tapePos & (kTape - 1)), first = std::min (n, kTape - at);
        for (auto& st : stemTape) for (auto& t : st)
        {
            juce::FloatVectorOperations::clear (t.data() + at, first);
            if (first < n) juce::FloatVectorOperations::clear (t.data(), n - first);
        }
    }
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
    const bool led = ! offline && ! ledOn && followClock (ahead);      // (a print goes by its own clock, whatever is plugged in; so does a rig led by another rig)
    const bool run = running.load();
    blockBpm = ledOn ? ledBpm : led ? fol.bpmSaid : tempoFor (cur[(size_t) cx::kTempo]);
    double beats = (double) n * blockBpm / 60.0 / sr;
    const bool starting = run && ! wasRunning, stopping = ! run && wasRunning;
    // controls set on a line of the song (ARM's mutes and solos): made here when the line is in this block or the next
    for (auto& q : setsAt)
        if (q.state.load() == 2)
        {
            if (! run) { q.state = 0; continue; }
            if (ppq + 2.0 * beats >= q.at) { set (q.ctl, q.v); q.state = 0; }
        }
    bool again = false;                                 // the outside clock went back to the start while this was playing

    // MIDI clock out. A follower's note for a beat has to be back here a block before that beat
    // is rendered, so the clock runs ahead of the sound: two blocks (one for the sender to be on
    // time, one for the note's way in) and LEAD, the follower's own delay. RUN waits that long.
    // Ticks go out only while it runs, as Live's did: the OXI One is set up for a clock that
    // starts with START, and falls back to its own when there is none. None while following.
    const bool clk = clockOn.load() && ! followOn.load() && ! offline;      // (and sends none)
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
    if (ledOn && run)
    {
        // Another rig's clock (this is the cue's rig): this buffer is the stretch of its song that it was told, so the
        // two are on the same sample of the same bar. A little out, it is made up inside this buffer (no step is
        // skipped or played twice); far out (the other went somewhere else), it goes there.
        const bool jump = ! starting && std::abs (ledFrom - ppq) > 0.25;
        if (starting || jump) { ppq = ledFrom; again = jump; }
        beats = juce::jlimit (0.0, 0.5, ledEnd - ppq);
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
    if (starting) startsV.fetch_add (1);
    if (starting || again) { ++runCount; barMoved = true; pushHeard ({ ppq, 0, 0, 0, 2 }); }
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
    advanceControls (beats, n);
    gatherNotes (n, spb);

    const int keyIdx = (int) std::lround (snd[(size_t) cx::kKey] * 12.0f);
    sf::Bus::get().d().keyRoot.store (keyIdx - 1);
    const bool pan = panicNow.exchange (false);

    for (auto* b : { &mix, &sendA, &sendB, &busBuf }) b->clear (0, n);
    // who is heard: a soloed lane silences the other lanes; a soloed return silences every lane's dry signal
    bool anySolo = false;
    for (int l = 0; l < kLanes; ++l) anySolo = anySolo || snd[(size_t) cx::lane (l, cx::kSolo)] > 0.5f;
    const bool retSolo[2] = { snd[(size_t) cx::kRetMS + 1] > 0.5f, snd[(size_t) cx::kRetMS + 3] > 0.5f };
    const bool anyRetSolo = retSolo[0] || retSolo[1];
    const float dryNow = anyRetSolo ? 0.0f : 1.0f;
    const float fall = (float) (30.0 * n / sr);      // meters fall 30 dB a second
    // Every lane is played into its own pair of channels first: POCKETS hears them all together
    // before any of them reaches the mix.
    for (int l = 0; l < kLanes; ++l)
    {
        auto& ln = lane[(size_t) l];
        juce::AudioBuffer<float> b2 (laneBuf.getArrayOfWritePointers() + 2 * l, 2, n);
        if (! run && ln.patWait.load()) { ln.patWait = false; ln.patDirty = true; }      // the clock stopped: nothing to wait for
        bool edited = false;
        if (ln.patDirty.load())
        {
            const juce::SpinLock::ScopedTryLockType g (patLock);
            if (g.isLocked()) { ln.player.pat = ln.shared; ln.patDirty = false; edited = true; }
        }
        ln.midi.clear();
        if (stopping || pan || starting || again) ln.player.stop (ln.midi, 0);
        auto* voice = ln.voice.load();
        auto* voiceOut = ln.voiceOut.load();
        // the pitch wheel: the lane's pitch comes to where the wheel is over a few thousandths of a second
        {
            const float want = bendWant[(size_t) l].load() * kBendSemis;
            float& now = bendNow[(size_t) l];
            if (now != want) { now += (want - now) * (1.0f - std::exp (-(float) n / (0.004f * (float) sr))); if (std::abs (want - now) < 1.0e-4f) now = want; }
            if (auto* v = ln.voice.load()) v->proc->bendSt = now;
            if (voiceOut != nullptr) voiceOut->proc->bendSt = now;
        }
        if (const auto moved = ln.knobsMoved.load (std::memory_order_relaxed); voice != ln.voiceSeen || moved != ln.knobsSeen)
        {
            // a voice that has just taken the lane, or whose model's knobs are others now: every knob as it stands (it
            // was copied a moment before it was put here)
            ln.voiceSeen = voice; ln.knobsSeen = moved;
            for (int k = 0; k < kLaneKnobs; ++k) applyBind (voice->bind (k), snd[(size_t) cx::lane (l, k)]);
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
        const bool seqOff = snd[(size_t) cx::lane (l, cx::kSeqMute)] > 0.5f;
        if (seqOff && ! ln.seqOff) ln.player.stop (ln.midi, 0);
        const bool pitchedWas = ln.player.pitched;
        if (auto* v = ln.voice.load()) ln.player.pitched = v->plays != nullptr ? v->plays->load() >= 0.5f : lanes()[(size_t) l].pitched;     // (PLAYS: NOTES plays each step's note, HITS each at C3)
        if (auto* v = ln.voice.load()) ln.player.selfTie = ln.player.pitched && v->proc != nullptr && v->proc->mod().holdsNotes();     // (a lone step longer than its sequence: a drone, but not of a sample that plays once, nor of a drum)
        ln.seqOff = seqOff;
        // (the hundred and fifteenth build) a step edited in that reaches over now sounds now; PLAYS turned, or a setting
        // heard from the next note changed, and what the pattern holds is struck again (a drone's next time round may be
        // bars away)
        // (the hundred and seventeenth build) an arrangement's BREAK: from brkFrom to brkTo (the song's beats) no lane's steps
        // are played, what they hold ends on its first sample, and whatever sounds on (an instrument's release, the effects'
        // tails, the returns) passes through. Notes from outside still play.
        const double bF = brkFrom.load (std::memory_order_relaxed), bT = brkTo.load (std::memory_order_relaxed);
        const bool brkSet = run && bT > bF;
        const bool inBrk = brkSet && ppq >= bF - 1e-9 && ppq < bT - 1e-9;
        {
            const bool again2 = ln.restrike.exchange (false) || ln.player.pitched != pitchedWas;
            if (run && ! starting && ! seqOff && ! inBrk && ! ln.patWait.load() && (edited || again2)) ln.player.chase (ppq, again2, ln.midi);
        }
        // the steps of [a, b), less any of the BREAK: it stops what sounds on its first sample, and what the pattern holds over
        // its end is struck there
        auto steps = [&] (double a, double b2)
        {
            if (! brkSet || b2 <= bF + 1e-12 || a >= bT - 1e-12) { ln.player.render (a, b2, spb, n, ln.midi, ppq); return; }
            if (a < bF) ln.player.render (a, bF, spb, n, ln.midi, ppq);
            if (! ln.inBreak) { ln.player.stop (ln.midi, juce::jlimit (0, std::max (0, n - 1), (int) std::floor ((std::max (a, bF) - ppq) * spb + 1e-6))); ln.inBreak = true; }
            if (b2 > bT)
            {
                ln.inBreak = false;
                ln.player.chase (bT, false, ln.midi);
                ln.player.render (bT, b2, spb, n, ln.midi, ppq);
            }
        };
        if (! brkSet) ln.inBreak = false;
        if (run)
        {
            // a recalled pattern is taken up on the bar line, to the sample: the block is rendered round it
            const double to = ppq + beats, bar = std::ceil (ppq / 4.0 - 1e-9) * 4.0;
            // (one that waits for a travel to end: the travel sets off on the next beat, and the first bar line at or after
            // its end is the one)
            if (ln.patWait.load() && ln.patFresh.exchange (false))
            {
                const double after = (double) ln.patAfter.load();
                ln.patDue = after > 0.0 ? std::ceil ((std::ceil (ppq - 1e-9) + after) / 4.0 - 1e-6) * 4.0 : -1.0e18;
            }
            if (ln.patWait.load() && bar < to - 1e-12 && bar >= ln.patDue - 1e-6)
            {
                if (bar > ppq && ! seqOff) steps (ppq, bar);
                {
                    const juce::SpinLock::ScopedTryLockType g (patLock);
                    if (g.isLocked()) ln.player.pat = ln.shared; else ln.patDirty = true;      // (the screen is writing it: next block)
                }
                ln.patWait = false;
                if (! seqOff) steps (std::max (ppq, bar), to);
            }
            else if (! seqOff) steps (ppq, to);
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
            if (fade && voiceOut != nullptr && voiceOut != voice)       // (the same one twice: a lane being given back, for a moment)
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
            const float g1 = snd[(size_t) cx::insMute (l, sl)] > 0.5f ? 0.0f : 1.0f, g0 = ln.slotG[sl];
            ln.slotG[sl] = g1;
            if (in == nullptr && ! fade) continue;
            if (in != nullptr)
            {
                // SOLO on an insert: only what the effect makes (its MIX held at full, its DRY switch off)
                const bool solo = snd[(size_t) cx::insSolo (l, sl)] > 0.5f;
                if (solo != in->soloOn)
                {
                    in->soloOn = solo;
                    if (solo && in->dryRaw != nullptr) in->dryWas = in->dryRaw->load();
                    if (in->mixCell >= 0) applyBind (in->bind[in->mixCell], solo ? 1.0f : snd[(size_t) cx::ins (l, sl, in->mixCell)]);
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
        const bool eqOn = eqUpdate (ln, l, snd.data());
        const int dly = ln.delay.load();
        // (what the EQ costs, for the lane being watched: its level after against before, over the last seconds)
        const bool costs = eqWatch.load (std::memory_order_relaxed) == l;
        double sqPre = 0.0, sqPost = 0.0;
        if (costs) for (int ch = 0; ch < 2; ++ch) { const float* x = b2.getReadPointer (ch); for (int i = 0; i < n; ++i) sqPre += (double) x[i] * x[i]; }
        if (eqOn)
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = b2.getWritePointer (ch);
                for (int e = 0; e < kEqBands; ++e) if (ln.eqLive[e]) for (int i = 0; i < n; ++i) x[i] = ln.eq[e].tick (x[i], ch);
            }
        if (costs)
        {
            for (int ch = 0; ch < 2; ++ch) { const float* x = b2.getReadPointer (ch); for (int i = 0; i < n; ++i) sqPost += (double) x[i] * x[i]; }
            if (eqCostLane != l) { eqCostLane = l; eqPre = eqPost = 0.0; }
            const double a = std::exp (-(double) n / (1.5 * sr));
            eqPre = a * eqPre + (1.0 - a) * sqPre / (double) n; eqPost = a * eqPost + (1.0 - a) * sqPost / (double) n;
            eqCostDb = eqPre > 1.0e-10 ? (float) (10.0 * std::log10 ((eqPost + 1.0e-20) / eqPre)) : 0.0f;
        }
        // the pocket, where the lane has it after its effects (and its shape, for the lane being watched)
        postPocket (l, b2, n, false);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = b2.getWritePointer (ch);
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
        const bool heard = snd[(size_t) cx::lane (l, cx::kMute)] < 0.5f && (! anySolo || snd[(size_t) cx::lane (l, cx::kSolo)] > 0.5f) && (so < 0 || so == l);
        // (and its fader, the ninety-fifth build: as it comes, 1 exactly, and the lane is left as it was, to the bit)
        {
            const float want = mixGainOf (mixPlace[(size_t) l].load (std::memory_order_relaxed));
            mixNow[l] += 0.35f * (want - mixNow[l]);
            if (std::abs (want - mixNow[l]) < 1.0e-5f) mixNow[l] = want;
        }
        const float lg = heard ? mixNow[l] : 0.0f;
        if (so >= 0 && so != l) ln.gainWas = 0.0f;       // a test's solo is in force from the first sample
        if (lg != 1.0f || ln.gainWas != 1.0f) for (int ch = 0; ch < 2; ++ch) b2.applyGainRamp (ch, 0, n, ln.gainWas, lg);
        ln.gainWas = lg;
    }

    // POCKETS: each lane is carved where it covers a sound with the better claim to that part of the
    // spectrum, while that sound is there. What is not heard (muted, or put out by a solo) covers
    // nothing. With AMOUNT at nought, and the screen not showing it, this does nothing at all.
    {
        const auto tk = clock();
        pockets.lanes (laneBuf.getArrayOfWritePointers(), n, pocketSet(), pkWatch.load());
        profPockets += since (tk);
    }

    for (int l = 0; l < kLanes; ++l)
    {
        auto& ln = lane[(size_t) l];
        juce::AudioBuffer<float> b2 (laneBuf.getArrayOfWritePointers() + 2 * l, 2, n);
        const float lp = b2.getMagnitude (0, n);
        ln.hold = std::max (juce::Decibels::gainToDecibels (lp, -100.0f), ln.hold - fall);
        laneDb[(size_t) l] = ln.hold;
        mixDb[(size_t) l] = ln.hold;                // (the mix page's meter: the same, after the fader)
        laneMax[(size_t) l] = std::max (laneMax[(size_t) l], lp);
        idleSaw (idleDb[(size_t) l], juce::Decibels::gainToDecibels (lp, -100.0f));     // (THE IDLE WATCH)
        {
            // its loudness as it reaches the mix; and, while a lane's low end is being watched, every lane's level under
            // 200 Hz across the bar
            const float* a = b2.getReadPointer (0); const float* b = b2.getReadPointer (1);
            auto& m = loud->lane[l];
            for (int i = 0; i < n; ++i) m.push (a[i], b[i]);
            laneLufs[(size_t) l] = m.shortTerm;
            if (lowWatch.load (std::memory_order_relaxed) >= 0)
            {
                const double bps = blockBpm / 60.0 / sr, t0 = ppq - (double) maxLat.load() * bps;
                const float k = 1.0f - std::exp (-6.2831853f * 200.0f / (float) sr), rel = std::exp (-1.0f / (0.03f * (float) sr));       // (its level: the peaks, let go over 30 ms)
                for (int i = 0; i < n; ++i)
                {
                    ln.lowZ[0] += k * (0.5f * (a[i] + b[i]) - ln.lowZ[0]); ln.lowZ[1] += k * (ln.lowZ[0] - ln.lowZ[1]);
                    const double q = (t0 + (double) i * bps) * 0.25;
                    const int bin = juce::jlimit (0, kLowBins - 1, (int) ((q - std::floor (q)) * (double) kLowBins));
                    if (bin != ln.lowBin) { if (ln.lowBin >= 0) lowBins[(size_t) l][(size_t) ln.lowBin] = ln.lowAcc; ln.lowBin = bin; ln.lowAcc = 0.0f; }
                    ln.lowEnv = std::max (std::abs (ln.lowZ[1]), ln.lowEnv * rel);
                    ln.lowAcc = std::max (ln.lowAcc, ln.lowEnv);
                }
            }
        }
        for (int ch = 0; ch < 2; ++ch) { const float* x = b2.getReadPointer (ch); double q = 0.0; for (int i = 0; i < n; ++i) q += (double) x[i] * x[i]; laneSq[(size_t) l] += q; }

        {
            // the lane as it goes into the mix (a soloed return silences it there, and so here)
            if (dryMixWas == 1.0f && dryNow == 1.0f) stemOut (rec, 1 + l, b2.getReadPointer (0), b2.getReadPointer (1), n);
            else
            {
                for (int ch = 0; ch < 2; ++ch) recBuf.copyFromWithRamp (ch, 0, b2.getReadPointer (ch), n, dryMixWas, dryNow);
                stemOut (rec, 1 + l, recBuf.getReadPointer (0), recBuf.getReadPointer (1), n);
            }
        }
        const float sa = snd[(size_t) cx::lane (l, cx::kSendA)], sb = snd[(size_t) cx::lane (l, cx::kSendB)];
        const float ga = sa * sa, gb = sb * sb;
        // The drum bus: a lane its CONFIG names goes to the mix through it, with the others it
        // names. (A switch thrown crosses the lane over in one buffer.) The sends leave before it.
        const float onNow = laneOnBus (l) ? 1.0f : 0.0f, onWas = busWas[(size_t) l];
        busWas[(size_t) l] = onNow;
        if (recMore) rec->lanePaths (l, b2.getReadPointer (0), b2.getReadPointer (1), n, dryMixWas * (1.0f - onWas), dryNow * (1.0f - onNow), dryMixWas * onWas, dryNow * onNow);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (onWas < 1.0f || onNow < 1.0f) mix.addFromWithRamp (ch, 0, b2.getReadPointer (ch), n, dryMixWas * (1.0f - onWas), dryNow * (1.0f - onNow));
            if (onWas > 0.0f || onNow > 0.0f) busBuf.addFromWithRamp (ch, 0, b2.getReadPointer (ch), n, dryMixWas * onWas, dryNow * onNow);
            sendA.addFromWithRamp (ch, 0, b2.getReadPointer (ch), n, ln.sendWas[0], ga);
            sendB.addFromWithRamp (ch, 0, b2.getReadPointer (ch), n, ln.sendWas[1], gb);
        }
        ln.sendWas[0] = ga; ln.sendWas[1] = gb;
    }
    dryMixWas = dryNow;

    juce::AudioBuffer<float> a2 (sendA.getArrayOfWritePointers(), 2, n), bb2 (sendB.getArrayOfWritePointers(), 2, n), m2 (mix.getArrayOfWritePointers(), 2, n);
    // TRANSITION: its lead-in and its hit, into the mix (before the master) and by its two amounts into the returns
    {
        float* const tl = transBuf.getWritePointer (0); float* const tr = transBuf.getWritePointer (1);
        juce::FloatVectorOperations::clear (tl, n); juce::FloatVectorOperations::clear (tr, n);
        const bool transSounds = trans.render (tl, tr, n, run, ppq, beats);
        stemOut (rec, 1 + kLanes + 3, tl, tr, n);          // (its own stem, silent or not: the stems together are what the master is given)
        if (transSounds)
        {
            const float ga = trans.toA.load(), gb = trans.toB.load();
            for (int ch = 0; ch < 2; ++ch)
            {
                m2.addFrom (ch, 0, transBuf, ch, 0, n);
                if (ga > 0.0f) a2.addFrom (ch, 0, transBuf, ch, 0, n, ga);
                if (gb > 0.0f) bb2.addFrom (ch, 0, transBuf, ch, 0, n, gb);
            }
        }
    }
    {
        // The lanes on the drum bus, together, through it and on to the mix. At rest the bus
        // leaves them exactly as they came (and what it is recorded as doing is silence).
        auto tb = clock();
        juce::AudioBuffer<float> d2 (busBuf.getArrayOfWritePointers(), 2, n);
        for (int ch = 0; ch < 2; ++ch) busDry.copyFrom (ch, 0, d2, ch, 0, n);
        if (pan) busProc->panic();
        busProc->processBlock (d2, noMidi);
        // (its fader, as it goes into the mix: what the bus is recorded as doing has it too, so the stems still add up)
        {
            const float want = mixGainOf (mixPlace[(size_t) kMixBus].load (std::memory_order_relaxed));
            mixNow[kMixBus] += 0.35f * (want - mixNow[kMixBus]);
            if (std::abs (want - mixNow[kMixBus]) < 1.0e-5f) mixNow[kMixBus] = want;
            if (mixNow[kMixBus] != 1.0f || mixWas[kMixBus] != 1.0f) for (int ch = 0; ch < 2; ++ch) d2.applyGainRamp (ch, 0, n, mixWas[kMixBus], mixNow[kMixBus]);
            mixWas[kMixBus] = mixNow[kMixBus];
            mixHold[kMixBus] = std::max (juce::Decibels::gainToDecibels (d2.getMagnitude (0, n), -100.0f), mixHold[kMixBus] - fall);
            mixDb[(size_t) kMixBus] = mixHold[kMixBus];
            idleSaw (idleDb[(size_t) kMixBus], juce::Decibels::gainToDecibels (d2.getMagnitude (0, n), -100.0f));
        }
        // (the hundred and eleventh build: the bus as it goes into the mix, after its fader, so PRINT's stems add up to the mix)
        if (recMore) rec->also (RecSink::kBusOut, d2.getReadPointer (0), d2.getReadPointer (1), n);
        for (int ch = 0; ch < 2; ++ch) m2.addFrom (ch, 0, d2, ch, 0, n);
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* w = busDry.getWritePointer (ch);
                const float* x = d2.getReadPointer (ch);
                for (int i = 0; i < n; ++i) w[i] = x[i] - w[i];
            }
            stemOut (rec, 1 + kLanes + 2, busDry.getReadPointer (0), busDry.getReadPointer (1), n);
        }
        profMaster += since (tb);
    }
    // SPACE changing reverbs: the one leaving fades over this block; the one arriving is emptied of
    // whatever it held when it last ran, and comes up once that is done.
    const int spaceNext = spaceWant.load();
    const bool spaceSwap = spaceNext != spaceNow;
    if (pan) { retA->panic(); space[(size_t) spaceNow]->panic(); }
    auto tr = clock();
    retA->processBlock (a2, noMidi);
    {
        // (the hundred and seventh build) DUB TO SPACE: what DUB sounds, by its fader and its MUTE (not its SOLO: soloing
        // SPACE still hears DUB in it), on into SPACE with the lanes' sends: the echoes into the reverb. Before POCKETS, so
        // what goes in is not a tail already carved. At nought nothing is added and SPACE is what it was.
        const float want = snd[(size_t) cx::kRetMS] < 0.5f ? toSpaceGain (dubToSpace.load (std::memory_order_relaxed)) * mixGainOf (mixPlace[(size_t) kMixDub].load (std::memory_order_relaxed)) : 0.0f;
        if (want > 0.0f || toSpaceWas > 0.0f)
            for (int ch = 0; ch < 2; ++ch) bb2.addFromWithRamp (ch, 0, a2.getReadPointer (ch), n, toSpaceWas, want);
        toSpaceWas = want;
    }
    space[(size_t) spaceNow]->processBlock (bb2, noMidi);
    profRet += since (tr);
    {
        // POCKETS: the returns have no pocket, and give way to every lane
        const auto tk = clock();
        float* const wet[4] = { a2.getWritePointer (0), a2.getWritePointer (1), bb2.getWritePointer (0), bb2.getWritePointer (1) };
        pockets.returns (wet, n);
        profPockets += since (tk);
    }
    const bool spaceQuiet = spaceSwap || spaceHold > 0;
    spaceHold = std::max (0, spaceHold - n);
    if (spaceSwap) { spaceNow = spaceNext; space[(size_t) spaceNow]->panic(); spaceHold = (int) (sr * 0.02); }
    // (each return's fader with its mute: as it comes, 1 exactly)
    for (int k = 0; k < 2; ++k)
    {
        const float want = mixGainOf (mixPlace[(size_t) (kMixDub + k)].load (std::memory_order_relaxed));
        mixNow[kMixDub + k] += 0.35f * (want - mixNow[kMixDub + k]);
        if (std::abs (want - mixNow[kMixDub + k]) < 1.0e-5f) mixNow[kMixDub + k] = want;
    }
    const float rg[2] = { snd[(size_t) cx::kRetMS] < 0.5f && (! anyRetSolo || retSolo[0]) ? mixNow[kMixDub] : 0.0f,
                          snd[(size_t) cx::kRetMS + 2] < 0.5f && (! anyRetSolo || retSolo[1]) && ! spaceQuiet ? mixNow[kMixSpace] : 0.0f };
    for (int k = 0; k < 2; ++k)
    {
        const float pk = (k == 0 ? a2 : bb2).getMagnitude (0, n) * rg[k];
        mixHold[kMixDub + k] = std::max (juce::Decibels::gainToDecibels (pk, -100.0f), mixHold[kMixDub + k] - fall);
        mixDb[(size_t) (kMixDub + k)] = mixHold[kMixDub + k];
        idleSaw (idleDb[(size_t) (kMixDub + k)], juce::Decibels::gainToDecibels (pk, -100.0f));
    }
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
    for (int k = 0; k < 2; ++k)
    {
        // each return as it goes into the mix
        const auto& wet = k == 0 ? a2 : bb2;
        for (int ch = 0; ch < 2; ++ch) recBuf.copyFromWithRamp (ch, 0, wet.getReadPointer (ch), n, retGainWas[k], rg[k]);
        stemOut (rec, 1 + kLanes + k, recBuf.getReadPointer (0), recBuf.getReadPointer (1), n);
    }
    retGainWas[0] = rg[0]; retGainWas[1] = rg[1];

    float inPk = 0.0f;
    tr = clock();
    // (the hundred and eleventh build) the mix with none of the master on it, its EQ and GAIN not either: PRINT's MIX BEFORE
    // THE MASTER, which its stems add up to (it was taken after them)
    if (recMore) rec->also (RecSink::kMasterIn, m2.getReadPointer (0), m2.getReadPointer (1), n);
    // The master's EQ (the ninety-fifth build): on the whole mix, before GAIN and the master's chain, so TAPE, COMP and
    // LIMIT are given what it has shaped and the limiter still has the last word. With no band in it does nothing at all.
    if (eqUpdate (masterEq, -1, snd.data()))
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = m2.getWritePointer (ch);
            for (int e = 0; e < kEqBands; ++e) if (masterEq.eqLive[e]) for (int i = 0; i < n; ++i) x[i] = masterEq.eq[e].tick (x[i], ch);
        }
    // GAIN: the whole mix, as it goes into the master chain (in the middle, where it starts: untouched)
    {
        const float db = gainDb (snd[(size_t) cx::kGain]);
        const float g1 = std::abs (db) < 0.01f ? 1.0f : std::pow (10.0f, db * 0.05f);
        if (g1 != 1.0f || mixGainWas != 1.0f) for (int ch = 0; ch < 2; ++ch) m2.applyGainRamp (ch, 0, n, mixGainWas, g1);
        mixGainWas = g1;
    }
    for (size_t k = 0; k < master.size(); ++k)
    {
        if (k + 1 == master.size()) { inPk = m2.getMagnitude (0, n); if (recMore) rec->also (RecSink::kLimiterIn, m2.getReadPointer (0), m2.getReadPointer (1), n); }
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
    // (the ninety-fifth build: momentary and integrated with it; the integrated figure starts again when RUN is pressed
    // and when the mix page's readout is)
    if (lufsResetAsk.exchange (false) || (run && ! lufsRan)) loud->m.resetIntegrated();
    lufsRan = run;
    lufsM = loud->m.momentary; lufsI = loud->m.integrated;
    const float outPk = std::max (pk[0], pk[1]);
    const float gr = inPk > 1e-4f && outPk > 1e-5f ? juce::jlimit (-24.0f, 0.0f, juce::Decibels::gainToDecibels (outPk / inPk)) : 0.0f;
    juce::ignoreUnused (gr);
    // what the limiter itself says it is doing (the figure here was the output's peak against the input's, which the
    // clipper, the automatic trim and the limiter's own delay were all in)
    if (! master.empty())
    {
        const auto& lm = master.back()->mod();
        limGr = lm.meter (0); clipGr = lm.meter (1); autoTrim = lm.meter (2); truePeak = lm.meter (3); trimWanted = lm.meter (4);
    }
    gainRed = gainRed.load() + 0.2f * (-limGr.load() - gainRed.load());

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* s = m2.getReadPointer (ch);
        float* d = out.getWritePointer (ch);
        for (int i = 0; i < n; ++i) d[i] = std::isfinite (s[i]) ? juce::jlimit (-1.0f, 1.0f, s[i]) : 0.0f;
    }
    idleSaw (idleMaster, juce::Decibels::gainToDecibels (out.getMagnitude (0, n), -100.0f));
    idleFor = run ? 0.0 : idleFor + (double) n / sr;
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
    if (auto* f = cueFeed.load())
    {
        // where the next buffer starts, for the cue's rig to render the same stretch of the song ahead of it
        const uint32_t q = f->seq.load (std::memory_order_relaxed);
        f->seq.store (q + 1u, std::memory_order_release);
        f->devPos.store (samplesDone, std::memory_order_relaxed);
        f->ppq.store (ppq, std::memory_order_relaxed);
        f->bpm.store (blockBpm, std::memory_order_relaxed);
        f->running.store (run ? 1 : 0, std::memory_order_relaxed);
        f->block.store (devBlock > 0 ? devBlock : n, std::memory_order_relaxed);
        f->seq.store (q + 2u, std::memory_order_release);
    }
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    prepare (d->getCurrentSampleRate(), std::max (64, d->getCurrentBufferSizeSamples()));
    overCount = 0; cpuMost = 0.0;
    // (on the Mac the device's own figure already holds one buffer)
    devBufMs = (double) d->getCurrentBufferSizeSamples() * 1000.0 / d->getCurrentSampleRate();
    devBlock = d->getCurrentBufferSizeSamples();
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
    cpuMost = std::max (used / avail, cpuMost.load() * std::exp (-avail / 4.0));
    if (used > avail) { overCount.fetch_add (1); overAt = juce::Time::getMillisecondCounter(); }
}
} // namespace sf0
