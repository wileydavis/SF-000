#include "Processor.h"
#include "BandLimited.h"
#include "E16View.h"
#include "Editor.h"
#include "Registry.h"
#include <chrono>
#include <cstring>
#include <random>
#include <thread>

using namespace sf;

static double posMod (double x, double m)
{
    const double r = std::fmod (x, m);
    return r < 0.0 ? r + m : r;
}

// SF CONTROL's SLOW ALL for a host block starting at ppq0 (blockBeats long): a change SF CONTROL
// made in this same host block counts from the next one, in every plugin, whichever side of SF
// CONTROL Live processes it on (Bus.h). The three values are read as one (slowSeq); a writer that
// never finishes can't hold up the audio thread. passed: the change (its slowSeq) whose song
// position this plugin has already reached, or left behind with the transport stopped: its fence
// is spent here, and returning to that position later (a loop, a relocate) takes the new length.
// (SF CONTROL clears the fence in its next block as well.)
static int slowAllFor (const BusData& d, bool playing, double ppq0, double blockBeats, uint32_t& passed)
{
    for (int tries = 0; tries < 8; ++tries)
    {
        const uint32_t s0 = d.slowSeq.load();
        if ((s0 & 1u) != 0) continue;
        const int now = d.slowAll.load(), before = d.slowAllPrev.load();
        const double from = d.slowAllFromPpq.load();
        if (d.slowSeq.load() != s0) continue;
        if (from < 0.0) return juce::jlimit (0, kSlowAllTop, now);
        if (! playing || ppq0 >= from - 1e-9) passed = s0;
        const bool sameBlock = passed != s0 && ppq0 < from - 1e-9 && ppq0 > from - 2.0 * blockBeats;
        return juce::jlimit (0, kSlowAllTop, sameBlock ? before : now);
    }
    return juce::jlimit (0, kSlowAllTop, (int) d.slowAll.load());
}

juce::AudioProcessor::BusesProperties SFProcessor::busesFor (const Module& m)
{
    BusesProperties b;
    if (m.instrument())
        b = b.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    else
        b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true)
             .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    if (m.sidechain())
        b = b.withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
    for (auto& name : m.extraOutputs())
        b = b.withOutput (name, juce::AudioChannelSet::stereo(), false);
    return b;
}

// MOTION can move any visible continuous parameter of the module.
static std::vector<int> motionTargetsFor (const std::vector<ParamSpec>& p)
{
    std::vector<int> t;
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i].type == PType::Float && ! p[i].hidden) t.push_back ((int) i);
    return t;
}

static std::vector<ParamSpec> withSystem (std::vector<ParamSpec> s, bool instrument, const std::vector<int>& targets)
{
    std::vector<std::string> tnames { "OFF" };
    for (int i : targets) tnames.push_back (s[(size_t) i].name);
    auto g = choice ("slow", "SLOW", SFProcessor::glideNames(), 0); g.song = false; g.preset = false;
    auto ms = choice ("macro_src", "MACRO SRC", { "OFF", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "MACRO 5", "MACRO 6" }, 0); ms.song = false; ms.preset = false;
    auto ma = knob ("macro_amt", "MACRO AMT", -1.0f, 1.0f, 0.5f, "%"); ma.song = false; ma.glide = false; ma.preset = false;
    s.push_back (g); s.push_back (ms); s.push_back (ma);
    for (int k = 1; k <= SFProcessor::kMotions; ++k)
    {
        const std::string id = "m" + std::to_string (k), nm = "M" + std::to_string (k);
        s.push_back (choice ((id + "_src").c_str(), (nm + " SOURCE").c_str(), motionSourceNames (instrument), 0));
        s.push_back (choice ((id + "_rate").c_str(), (nm + " RATE").c_str(), motionRateNames(), 4));
        s.push_back (choice ((id + "_target").c_str(), (nm + " TARGET").c_str(), tnames, 0));
        auto dp = knob ((id + "_depth").c_str(), (nm + " DEPTH").c_str(), -1.0f, 1.0f, 0.0f, "%");
        dp.glide = false;
        s.push_back (dp);
    }
    return s;
}

SFProcessor::SFProcessor (ModulePtr m)
    : juce::AudioProcessor (busesFor (*m)),
      module (std::move (m)),
      motionTargets (motionTargetsFor (module->params())),
      specs (withSystem (module->params(), module->instrument(), motionTargets)),
      numModParams ((int) module->params().size()),
      numExtraOut ((int) module->extraOutputs().size()),
      apvts (*this, nullptr, "PARAMS", makeLayout())
{
    jassert (specs.size() <= (size_t) kMaxParams);
    for (auto& s : specs)
    {
        params.push_back (apvts.getParameter (s.id));
        raw.push_back (apvts.getRawParameterValue (s.id));
    }
    cur.resize (specs.size());
    prev.resize (specs.size());
    base.resize (specs.size());
    macroOff.assign (specs.size(), 0.0f);
    modAmt.assign (specs.size(), 0.0f);
    ovr = std::make_unique<std::atomic<float>[]> (specs.size());
    ovrFrom = std::make_unique<std::atomic<float>[]> (specs.size());
    extGen = std::make_unique<std::atomic<uint32_t>[]> (specs.size());
    ovrGen = std::make_unique<std::atomic<uint32_t>[]> (specs.size());
    recallGen = std::make_unique<std::atomic<uint32_t>[]> (specs.size());
    reportedGen = std::make_unique<std::atomic<uint32_t>[]> (specs.size());
    reportExtGen = std::make_unique<std::atomic<uint32_t>[]> (specs.size());
    departGen = std::make_unique<std::atomic<uint32_t>[]> (specs.size());
    for (size_t i = 0; i < specs.size(); ++i) { ovr[i] = NAN; ovrFrom[i] = 0.0f; extGen[i] = 0; ovrGen[i] = 0; recallGen[i] = 0; reportedGen[i] = 0; reportExtGen[i] = 0; departGen[i] = 0; }
    ovrAge.assign (specs.size(), 0);
    for (size_t i = 0; i < specs.size(); ++i) cur[i] = prev[i] = base[i] = specs[i].def;
    defaultLocks();
    for (auto& h : songHas) h = false;
    for (auto& o : motionOut) o = 0.0f;

    module->setParam = [this] (int i, float v)
    {
        if (i < 0 || i >= (int) params.size()) return;
        auto* p = params[(size_t) i];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    };
    module->getParam = [this] (int i) { return i >= 0 && i < (int) raw.size() ? raw[(size_t) i]->load() : 0.0f; };
    module->getSongValue = [this] (int s, int i)
    {
        return s >= 0 && s < kSongs && i >= 0 && i < (int) specs.size() && songHas[(size_t) s].load() ? songVals[(size_t) s][(size_t) i] : NAN;
    };
    glideFrom.assign (specs.size(), 0.0f); glideTo.assign (specs.size(), NAN); glideElapsed.assign (specs.size(), 0.0); glideBeats.assign (specs.size(), 0.0);
    std::random_device rd;
    motionSeed = (uint32_t) rd() | 1u;
    instanceUid = (((uint64_t) rd() << 32) ^ (uint64_t) rd() ^ (uint64_t) (uintptr_t) this) | 1u;
    factory = sf::factoryPresets (module->id());
    chunkMidi.ensureSize (65536);
    sf::Bus::get();     // map the shared bus here, not on the audio thread
    sf::SincKernel::get();   // build the band-limited sample reader's table here, not on the audio thread
    for (size_t i = 0; i < params.size(); ++i)
    {
        jassert (params[i]->getParameterIndex() == (int) i);
        params[i]->addListener (this);
    }
    startTimerHz (30);
}

SFProcessor::~SFProcessor()
{
    stopTimer();
    for (auto* p : params) p->removeListener (this);
    sf::Bus::get().releaseGlide (glideSlot, instanceUid.load());
}

// GLIDE RINGS. The plugin's knobs on the E16 scene: those for its Live track and its module; with
// no track name (AU) or a renamed track, a module the scene has on one track only still finds its
// knobs. Each knob's ring position is Live's mapping of the parameter (normalised between lo and hi).
void SFProcessor::findE16Knobs()
{
    e16Seq = trackNameSeq.load();
    numE16 = 0;
    const std::string mod = module->id();
    char name[24];
    std::memcpy (name, trackName, sizeof (name));
    name[23] = 0;
    const char* track = sf::e16::trackFor (mod.c_str(), name);     // "": on several tracks, needs the name
    if (track == nullptr || track[0] == 0) return;
    for (const auto& k : sf::kE16Knobs)
        if (mod == k.module && std::strcmp (track, k.track) == 0 && k.param >= 0 && k.param < numModParams
            && numE16 < (int) e16Knobs.size())
        {
            auto* p = params[(size_t) k.param];
            e16Knobs[(size_t) numE16++] = { k.param, (uint32_t) k.channel << 24 | (uint32_t) k.number << 16, 0.0f,
                                            k.max > 0.0f ? juce::jmax (0.01f, p->convertTo0to1 (k.max)) : 1.0f };
        }
}

// Every block: the knobs travelling now and where their sound is, for SF CONTROL to show on the E16.
void SFProcessor::publishGlides (sf::Bus& bus)
{
    if (trackNameSeq.load() != e16Seq) findE16Knobs();
    if (numE16 == 0) return;
    const uint64_t me = instanceUid.load();
    if (glideSlot < 0 || bus.d().glides[glideSlot].owner.load() != me) glideSlot = bus.claimGlide (me);
    if (glideSlot < 0) return;
    auto& g = bus.d().glides[glideSlot];
    int n = 0;
    for (int k = 0; k < numE16 && n < sf::kGlideEntries; ++k)
    {
        const auto& e = e16Knobs[(size_t) k];
        if (glideBeats[(size_t) e.param] <= 0.0) continue;
        const float norm = params[(size_t) e.param]->convertTo0to1 (base[(size_t) e.param]);
        const float ring = juce::jlimit (0.0f, 1.0f, (norm - e.lo) / (e.hi - e.lo));
        g.entry[n++] = e.addr | (uint32_t) juce::roundToInt (ring * 16383.0f);
    }
    g.count = n;                 // after the entries it counts
    g.seenMs = sf::Bus::nowMs();
}

// Any thread: Live (automation, a MIDI-mapped knob, the host's own controls), the editor, a
// preset. Not this plugin's own report of a recalled value back to Live.
void SFProcessor::parameterValueChanged (int index, float newValue)
{
    if (index < 0 || index >= (int) specs.size()) return;
    if (index == notifying.load() && juce::MessageManager::existsAndIsCurrentThread()) return;
    const size_t i = (size_t) index;
    // A value away from Live's value at the recall is a move, newer than the recall, even if a
    // later one goes back to that value before the recall is reported (+3 then 0 dB, in one block).
    const float v = params[i]->convertFrom0to1 (newValue), from = ovrFrom[i].load();
    if (std::abs (v - from) > 1e-4f * (1.0f + std::abs (from))) departGen[i] = recallGen[i].load();
    extGen[i].fetch_add (1);
}

// A change in Live newer than the recall: it wins.
//   Once the recall is reported, Live holds the recalled value: any change it makes after the
//   report is the performer's (a knob, a reset, automation), whatever its value, the value from
//   before the recall included. (Compared with that value, a return to it used to count as an
//   echo, and the recall held on for the 3 s give-up while the knob showed the new setting.)
//   Until then Live still holds its value from before the recall and may send it again: only a
//   move away from it is newer (a count alone can't tell an echo from a move). A move away and
//   back again before the report is still a move (departGen): the value it ends on is the
//   performer's, even though it equals the one from before the recall.
bool SFProcessor::movedSinceRecall (size_t i) const
{
    const uint32_t e = extGen[i].load();
    if (reportedGen[i].load() == recallGen[i].load()) return e != reportExtGen[i].load();
    if (e == ovrGen[i].load()) return false;
    if (departGen[i].load() == recallGen[i].load()) return true;
    const float t = raw[i]->load(), from = ovrFrom[i].load();
    return std::abs (t - from) > 1e-4f * (1.0f + std::abs (from));
}

// Audio thread. Whether parameter i's recalled value is still in force, dropping it if not: Live
// reported it back, it was moved since, a state was loaded since, or Live still disagrees 3 s after
// it was told (the clock starts at the report: Live's message thread can be busy for seconds,
// saving the set). The one decision for the sound and for STORE, so a store never keeps a recall
// the sound has let go. advance: samples to add to the 3 s clock (0 for STORE).
bool SFProcessor::overrideHolds (size_t i, int advance)
{
    const float o = ovr[i].load();
    if (std::isnan (o)) return false;
    if (ovrState.load() != stateGen.load()) { ovr[i] = NAN; return false; }    // a state loaded since: newer, whatever its value
    const float t = raw[i]->load(), eps = 1e-4f * (1.0f + std::abs (t));
    if (reportedGen[i].load() == recallGen[i].load()) ovrAge[i] += advance;
    if (std::abs (t - o) < eps || movedSinceRecall (i) || ovrAge[i] > (int) (3.0 * sr)) { ovr[i] = NAN; return false; }
    return true;
}

// Any thread (the state save): whether a recalled value is what the sound uses now. The audio
// thread drops a recall that has given way (overrideHolds); this only reads.
bool SFProcessor::overrideInForce (size_t i) const
{
    return ! std::isnan (ovr[i].load()) && ovrState.load() == stateGen.load() && ! movedSinceRecall (i);
}

int SFProcessor::paramIndex (const std::string& id) const
{
    for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == id) return (int) i;
    return -1;
}

juce::AudioProcessorValueTreeState::ParameterLayout SFProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& s : specs)
    {
        const juce::ParameterID pid (s.id, 1);
        switch (s.type)
        {
            case PType::Float:
            {
                juce::NormalisableRange<float> r (s.min, s.max);
                if (! std::isnan (s.centre) && s.centre > s.min && s.centre < s.max) r.setSkewForCentre (s.centre);
                const auto unit = s.unit;
                auto toText = [unit] (float v, int)
                {
                    if (unit == "%")   return juce::String (juce::roundToInt (v * 100.0f)) + " %";
                    if (unit == "SLOT") return std::abs (v - 0.5f) < 0.04f ? juce::String ("AUTO")
                                             : v < 0.5f ? "SHARE " + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, (0.46f - v) / 0.46f) * 100.0f)) + " %"
                                                        : "HAND " + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, (v - 0.54f) / 0.46f) * 100.0f)) + " %";
                    if (unit == "SPLIT") return std::abs (v - 0.5f) < 0.02f ? juce::String ("OFF")
                                              : (v < 0.5f ? "MODEL LOWS " : "LAYER LOWS ") + juce::String (juce::roundToInt (std::min (1.0f, (std::abs (v - 0.5f) - 0.02f) / 0.48f) * 100.0f)) + " %";
                    if (unit == "BIP") return std::abs (v - 0.5f) < 0.02f ? juce::String ("OFF") : (v < 0.5f ? "LP " : "HP ") + juce::String (juce::roundToInt (std::abs (v - 0.5f) * 200.0f));
                    if (unit == "Hz")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
                    if (unit == "ms")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
                    if (unit == "dB")  return juce::String (v, 1) + " dB";
                    if (unit == "st")  return juce::String (v, 1) + " st";
                    if (unit == "s")   return juce::String (v, 2) + " s";
                    if (unit == "LUFS")return juce::String (v, 1) + " LUFS";
                    return juce::String (v, 2) + (unit.empty() ? "" : " " + juce::String (unit));
                };
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, r, s.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
                break;
            }
            case PType::Choice:
            {
                juce::StringArray c;
                for (auto& x : s.choices) c.add (x);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, c, (int) s.def));
                break;
            }
            case PType::Toggle:
            case PType::Momentary:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
                break;
        }
    }
    return layout;
}

bool SFProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (! module->instrument() && l.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != juce::AudioChannelSet::stereo()) return false;
    for (int i = module->instrument() ? 0 : 1; i < l.inputBuses.size(); ++i)
    {
        const auto& s = l.inputBuses[i];
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo() && s != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void SFProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    module->prepare (sampleRate, kChunk);
    setLatencySamples (module->latency());
    extraScratch.setSize (2 * std::max<int> (1, numExtraOut), kChunk);
    chunkMidi.ensureSize (65536);
    for (int k = 0; k < kMotions; ++k) motion[(size_t) k].prepare (sr, motionSeed + 0x9E3779B9u * (uint32_t) k);
    overSamples = 0;
    panicDown = false;
    panicLen = std::max (64, (int) std::lround (0.005 * sr));
    panicPos = panicInPos = panicLen;
    cutGain = 1.0f; cutEarly = -1.0; cutAtSeen = -2.0;
    streamPos = 0;
    firstBlock = true;
}

void SFProcessor::storeSong()
{
    storeAskGen = stateGen.load();         // first: a state loaded after this press is newer than it
    storeRequested = true;
}

void SFProcessor::applySong (int s)
{
    // Audio thread. The recalled values take effect now (ovr); the timer then tells Live, and
    // the override drops once Live's value matches.
    if (s < 0 || s >= kSongs || ! songHas[(size_t) s].load()) { songIndex = s; return; }
    const uint32_t sg = stateGen.load();      // the state this recall lands in (a load under way, or later: it is newer)
    for (size_t i = 0; i < specs.size(); ++i)
    {
        if (! specs[i].song) continue;
        ovrFrom[i] = raw[i]->load();          // the host's value at the recall
        ovrGen[i] = extGen[i].load();
        ovrAge[i] = 0;
        ovr[i] = songVals[(size_t) s][i];     // the message thread reads it first
        recallGen[i].fetch_add (1);           // after it: a report marked with this count carried this value
    }
    ovrState = sg;                            // before the report is asked for
    songIndex = s;
    notifyHost = true;
}

void SFProcessor::flushHostNotifications()
{
    if (! notifyHost.exchange (false)) return;
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const uint32_t g = recallGen[i].load();    // before the value: a newer recall is never marked reported
        const float v = ovr[i];
        if (std::isnan (v)) continue;
        // A state loaded since the recall (or loading): Live has the state's value, which is newer.
        // (Live loads a state on this thread, so a load never comes between this and the report.)
        if (ovrState.load() != stateGen.load()) continue;
        // Live's count of changes before the check: one made from here on is newer than the report.
        const uint32_t e0 = extGen[i].load();
        // Moved in Live after the recall (a knob turned before this timer came round): the newer
        // value stays, and the next audio block drops the recall for this parameter.
        if (movedSinceRecall (i)) continue;
        auto* p = params[i];
        notifying = (int) i;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
        notifying = -1;
        reportExtGen[i] = e0;                       // before reportedGen: read in that order
        reportedGen[i] = g;
    }
}

void SFProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    auto& bus = sf::Bus::get();
    auto& d = bus.d();

    // Transport, once per host block. When stopped, an internal clock stands in so tempo-synced
    // motion keeps running and every SF plugin agrees on the beat.
    Ctx c;
    c.sr = sr; c.bus = &bus;
    c.offline = isNonRealtime();
    c.trackName = trackName; c.trackNameSeq = trackNameSeq.load();
    c.uid = &instanceUid;
    c.bpm = lastBpm;
    bool havePos = false;
    double ppq0 = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { c.bpm = juce::jlimit (20.0, 400.0, *b); lastBpm = c.bpm; }
            if (auto ts = pos->getTimeSignature()) { c.beatsPerBar = juce::jlimit (0.5, 32.0, ts->numerator * 4.0 / std::max (1, ts->denominator)); lastBpb = c.beatsPerBar; }
            c.playing = pos->getIsPlaying();
            if (c.playing)
                if (auto q = pos->getPpqPosition())
                {
                    ppq0 = *q;
                    havePos = true;
                    if (auto lb = pos->getPpqPositionOfLastBarStart()) barPhase = posMod (*lb, c.beatsPerBar);
                }
        }
    if (! havePos)
    {
        // Stopped: continue from where Live stopped (or, at first use, from a wall-clock
        // anchor shared by every SF plugin) and advance by whole blocks: never jumps.
        if (! clockAnchored)
        {
            using namespace std::chrono;
            const double secs = duration<double> (steady_clock::now().time_since_epoch()).count();
            internalPpq = std::fmod (secs, 3600.0) * c.bpm / 60.0;
        }
        ppq0 = internalPpq;
    }
    const double bps = c.beatsPerSample();
    // This block starts before the last one ended: the transport jumped back (an Arrangement
    // loop wrapped, a relocate). Tempo changes move the position far less than 0.01 beat.
    const double lastEnd = internalPpq;
    const bool jumpedBack = c.playing && lastPlaying && ppq0 < lastEnd - 0.01;
    lastPlaying = c.playing;
    internalPpq = ppq0 + total * bps;
    clockAnchored = true;
    c.barPhase = barPhase;
    // KEY only while SF CONTROL is running: the shared bus outlives sets and Live sessions, so a
    // key left there by a deleted CONTROL (or the last set) must not transpose anything. The same
    // for SLOW ALL: a length left there must not slow the next set's knobs.
    const bool controlOn = bus.controlAlive();
    slowAllNow = controlOn ? slowAllFor (d, c.playing, ppq0, total * bps, slowPassed) : 0;
    // SF BUILD's cut (DROP), once per host block. Let go while the transport is stopped, after a
    // jump back past the press or a jump the wall clock doesn't follow (a loop wrapped), and when
    // BUILD has not run for a second (deleted, switched off).
    cutRead.read (d);
    {
        const double now = sf::Bus::nowSec(), secPerBeat = 60.0 / c.bpm;
        const bool buildOn = now - d.buildSeenSec.load() < 1.0;
        const bool current = cutRead.at >= 0.0 && ppq0 >= cutRead.from - 1e-6 && now - cutRead.wall <= (ppq0 - cutRead.from) * secPerBeat + 0.5;
        cutA = c.playing && buildOn && current ? cutRead.at : -1.0;
        cutB = cutA >= 0.0 ? cutRead.back : -1.0;
        if (cutA != cutAtSeen) { cutAtSeen = cutA; cutEarly = -1.0; }
    }

    // Song recall, store and panic requests from SF CONTROL (or this plugin's own buttons).
    if (! seqInit)
    {
        lastSongSeq = d.songSeq.load(); lastStoreSeq = d.storeSeq.load(); lastPanicSeq = d.panicSeq.load();
        seenStateGen = stateGen.load();
        seqInit = true;
        if (songIndex.load() < 0) songIndex = d.song.load();
    }
    // A state loaded since the last block (or loading now): newer than everything asked for before
    // it here. A song waiting for its bar line and a STORE not yet made are over, and SF CONTROL's
    // requests up to the load's end are passed over (later ones go ahead). Its recall in force has
    // given way already (overrideHolds), and the knobs start where the state has them: no glide.
    if (const uint32_t sg = stateGen.load(); sg != seenStateGen)
    {
        seenStateGen = sg;
        pendingSong = -1;
        pendingKey = -2;
        pendingAt = -1.0;
        pendingJumped = false;
        storeWanted = false;
        if (const uint32_t ls = songSeqAtLoad.load(); (int32_t) (ls - lastSongSeq) > 0) lastSongSeq = ls;
        if (const uint32_t lt = storeSeqAtLoad.load(); (int32_t) (lt - lastStoreSeq) > 0) lastStoreSeq = lt;
        firstBlock = true;
    }
    // A song still waiting when the transport jumps back (an Arrangement loop wrapped, a
    // relocate): SF CONTROL's bar line may never come, so this plugin's next bar line instead.
    // Jumped back again before that came (a loop with no bar line in it): here, at the jump.
    bool recallNow = false;
    if (jumpedBack && pendingSong.load() >= 0)
    {
        if (pendingJumped) recallNow = true;
        else { pendingAt = -1.0; pendingJumped = true; }
    }
    if (d.songSeq.load() != lastSongSeq)
    {
        lastSongSeq = d.songSeq.load();
        pendingSong = d.song.load();
        pendingAt = d.songAtPpq.load();
        pendingKey = d.songKey.load();
        pendingJumped = false;
        recallNow = false;
        // Asked for before the jump back this block made, seen only now (SF CONTROL asked in its
        // last block before an Arrangement loop wrapped; Live processed this plugin after the
        // wrap): as if the jump had come after the request, like everywhere else. Its bar line
        // lay where the transport jumped from (within a sample: a bar line between two samples
        // may end the loop's last block just past it): this plugin's next bar line. It lay
        // before (the loop ends just after a bar line): it has landed everywhere else, so here.
        if (jumpedBack && pendingAt >= 0.0 && d.songFromPpq.load() >= ppq0 + total * bps - 1e-9)
        {
            if (pendingAt >= lastEnd - 1.5 * bps) { pendingAt = -1.0; pendingJumped = true; }
            else recallNow = true;
        }
    }
    // SF CONTROL's STORE ALL (with its STORE TO song), or this plugin's own STORE (pressed in the
    // state in force: one from before a load is over). Made once the songs are free (songLock: a
    // state save or load has them for a moment), the next block at worst; never while a state is
    // being loaded (its values half in: the load ends the STORE, as one asked for before it).
    if (const uint32_t ss = d.storeSeq.load(); ss != lastStoreSeq) { lastStoreSeq = ss; storeTo = d.storeSong.load(); storeWanted = true; }
    if (storeRequested.exchange (false) && storeAskGen.load() == seenStateGen && ! storeWanted) { storeTo = -1; storeWanted = true; }
    bool storeNow = false;
    if (storeWanted && ! songLock.exchange (true, std::memory_order_acquire))
    {
        if ((stateGen.load() & 1u) == 0) storeNow = true;              // (a load turns it odd under songLock)
        else songLock.store (false, std::memory_order_release);
    }
    if (storeNow)
    {
        storeWanted = false;
        // Store into the selected song (the bus song), even if its recall is still waiting for the
        // bar; or into the song STORE TO names, which isn't recalled. That one is in force from
        // now if SF CONTROL made it the bus song (not while a song change is under way).
        const bool toOther = storeTo >= 0 && storeTo < kSongs;
        int s = toOther ? storeTo : d.song.load();
        if (s < 0) s = std::max (0, songIndex.load());
        if (! toOther || d.song.load() == s) songIndex = s;
        // What is in force: a value just recalled that Live hasn't reported back yet counts, unless
        // a newer change in Live has superseded it: the same decision the sound makes this block
        // (a recall, a newer knob move, then STORE in one host block used to store the recall
        // while the sound took the move).
        for (size_t i = 0; i < specs.size(); ++i) songVals[(size_t) s][i] = overrideHolds (i, 0) ? ovr[i].load() : raw[i]->load();
        songHas[(size_t) s] = true;
        songLock.store (false, std::memory_order_release);
    }
    bool panicNow = localPanic.exchange (false);
    if (const uint32_t ps = d.panicSeq.load(); ps != lastPanicSeq) { lastPanicSeq = ps; panicNow = true; }
    // While PANIC is held on SF CONTROL every plugin stays silent (and takes no new input).
    const bool panicHold = d.panicHeld.load() != 0 && bus.controlAlive();

    auto mainOut = getBusBuffer (buffer, false, 0);
    juce::AudioBuffer<float> sideAll;
    juce::AudioBuffer<float>* side = nullptr;
    const int sideBus = module->instrument() ? 0 : 1;
    if (module->sidechain() && getBusCount (true) > sideBus && getBus (true, sideBus)->isEnabled())
    {
        sideAll = getBusBuffer (buffer, true, sideBus);
        if (sideAll.getNumChannels() > 0) side = &sideAll;
    }

    recalledInBlock = false;
    for (int start = 0; start < total;)
    {
        // Chunks sit on a fixed grid of the stream (multiples of kChunk since prepare), so the
        // same performance renders bit-identically at any host buffer size.
        int n = std::min (kChunk - (int) (streamPos % (uint64_t) kChunk), total - start);
        c.ppq = ppq0 + start * bps;
        bool recalled = false;
        if (const int ps = pendingSong.load(); ps >= 0)
        {
            // Recall exactly on the bar line while playing (the chunk is cut there), at once when stopped.
            const double spb = c.samplesPerBeat();
            if (! c.playing || (recallNow && start == 0)) { applySong (ps); pendingSong = -1; recalled = true; }
            else if (pendingAt >= 0.0 && pendingAt - c.ppq < 2.0 * c.beatsPerBar)
            {
                // SF CONTROL's bar line: one target for every plugin, whatever order Live
                // processes them in. Seen late (a plugin processed before CONTROL in that
                // cycle): at once, a block late at most, still on the same bar.
                const double toBar = (pendingAt - c.ppq) * spb;
                if (toBar < 1e-3) { applySong (ps); pendingSong = -1; recalled = true; }
                else n = std::max (1, std::min (n, (int) std::ceil (toBar - 1e-3)));
            }
            else
            {
                // No target (or the transport jumped back past it): this plugin's next bar line.
                // Positions in samples relative to the bar line, with a 1e-3 sample tolerance for
                // the rounding of ppq (which grows with the song position): a bar line that falls
                // within the tolerance of a sample boundary is taken to be on it, so the recall can
                // never slip to the following bar.
                const double inBar = posMod (c.ppq - barPhase, c.beatsPerBar);
                const double sinceBar = inBar * spb, toBar = (c.beatsPerBar - inBar) * spb;
                constexpr double tol = 1e-3;
                if (sinceBar < 1.0 + tol || toBar < tol) { applySong (ps); pendingSong = -1; recalled = true; }
                else n = std::max (1, std::min (n, (int) std::ceil (toBar - tol)));
            }
        }
        // KEY. A song's key switches on the song's bar here too (CONTROL writes it to the bus in
        // its own bar chunk, which Live may process before or after this plugin): until then the
        // key in force stays, from then on the song's, until the bus agrees (1 s at most).
        // Otherwise (KEY turned by hand) the bus.
        recalledInBlock = recalledInBlock || recalled;
        if (recalled)
        {
            if (pendingKey != -2) { keyUsed = pendingKey; keyHold = (int) sr; }
            pendingKey = -2;
        }
        if (pendingSong.load() >= 0 && pendingKey != -2) {}
        else if (keyHold > 0) { if (d.keyRoot.load() == keyUsed) keyHold = 0; else keyHold -= n; }
        else keyUsed = d.keyRoot.load();
        c.keyRoot = keyUsed >= 0 && keyUsed < 12 && controlOn ? keyUsed : -1;
        c.n = n;
        c.samplePos = (int64_t) streamPos;
        processChunk (buffer, mainOut, side, start, n, c, midi, panicNow && start == 0, panicHold, start + n >= total);
        start += n;
        streamPos += (uint64_t) n;
    }
    firstBlock = false;
    publishGlides (bus);
    if (const int rs = resampleState.load (std::memory_order_acquire); rs == 1 || rs == 2)
        recordResample (mainOut, total, (int64_t) streamPos - total, ppq0, bps, c.playing, c.beatsPerBar);

    outMeter.push (mainOut.getArrayOfReadPointers(), mainOut.getNumChannels(), total);
    if (outMeter.peak > 0.999f) clipped = true;
    // Clear any remaining output channels (unused buses).
    if (numExtraOut == 0)
        for (int ch = mainOut.getNumChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, total);
}

void SFProcessor::updateParams (const Ctx& c, int n, float motionInput)
{
    const auto& d = c.bus->d();
    // SLOW: how long a move made now takes. CONTROL: SF CONTROL's SLOW ALL; a length: this
    // plugin's own; OFF: straight there.
    const int slow = (int) raw[(size_t) (numModParams + SYS_GLIDE)]->load();
    const double gBars = slow == SLOW_CONTROL ? glideBars (slowAllNow) : ownBars (slow);
    const int mSrc = (int) raw[(size_t) (numModParams + SYS_MACRO_SRC)]->load();
    const float mAmt = raw[(size_t) (numModParams + SYS_MACRO_AMT)]->load();
    const float macro = mSrc > 0 && mSrc <= kMacros ? d.macro[mSrc - 1].load() : 0.0f;
    const float smooth = std::exp (-(float) n / (float) (0.03 * sr));
    const double chunkBeats = n * c.beatsPerSample();

    // MOTION, evaluated at the end of this chunk (the module ramps from the previous value).
    std::fill (modAmt.begin(), modAmt.end(), 0.0f);
    const double ppqEnd = c.ppq + chunkBeats;
    for (int k = 0; k < kMotions; ++k)
    {
        const size_t base0 = (size_t) (numModParams + SYS_M1_SRC + k * 4);
        const int src = (int) std::lround (cur[base0]);
        if (src <= MOT_OFF || src >= kNumMotionSrc) { motion[(size_t) k].reset(); motionOut[(size_t) k] = 0.0f; continue; }
        const float m = motion[(size_t) k].tick (src, (int) std::lround (cur[base0 + 1]), c, ppqEnd, n, motionInput);
        motionOut[(size_t) k] = m;
        const int tgt = (int) std::lround (cur[base0 + 2]);
        if (tgt <= 0 || tgt > (int) motionTargets.size()) continue;
        modAmt[(size_t) motionTargets[(size_t) tgt - 1]] += cur[base0 + 3] * m;
    }

    for (size_t i = 0; i < specs.size(); ++i)
    {
        const auto& s = specs[i];
        prev[i] = cur[i];
        // The recalled value holds until Live reports it back, or gives way to a newer change in
        // Live (overrideHolds).
        float t = raw[i]->load();
        if (overrideHolds (i, n)) t = ovr[i].load();
        if (s.type != PType::Float) { cur[i] = t; base[i] = t; continue; }
        const float range = s.max - s.min;
        // SLOW: a performance-row knob (or a detail knob marked slow()) travels to a new setting (a
        // turn, automation, a song recall) over exactly N bars, however far. N is fixed when the
        // setting is made: changing SLOW or SLOW ALL never touches a knob already travelling, it only
        // sets how the next move goes. A knob moved again while it travels sets off from where it has got to.
        const bool glides = s.glidesWithSlow() && (int) i < numModParams;
        if (glides && ! firstBlock)
        {
            if (std::isnan (glideTo[i]) || std::abs (t - glideTo[i]) > 1e-5f * (1.0f + std::abs (range)))
            {
                glideFrom[i] = base[i];
                glideTo[i] = t;
                glideElapsed[i] = 0.0;
                glideBeats[i] = gBars * c.beatsPerBar;
            }
            if (glideBeats[i] > 0.0)
            {
                glideElapsed[i] += chunkBeats;
                const double k = std::min (1.0, glideElapsed[i] / glideBeats[i]);
                base[i] = k >= 1.0 ? glideTo[i] : glideFrom[i] + (glideTo[i] - glideFrom[i]) * (float) k;
                if (k >= 1.0) glideBeats[i] = 0.0;
            }
            else base[i] = t + smooth * (base[i] - t);
        }
        else
        {
            // Not a SLOW knob, or the first block (where the knobs start, not a move).
            glideTo[i] = glides ? t : NAN;
            glideBeats[i] = 0.0;
            base[i] = firstBlock ? t : t + smooth * (base[i] - t);
        }
        // MACRO and MOTION act on the knob's travel (normalised, skew included), after the glide,
        // so they stay immediate and move a frequency knob as evenly as the hand does.
        float off = modAmt[i];
        if (s.macroDir != 0 && (int) i < numModParams)
        {
            const float target = macro * mAmt * (float) s.macroDir;
            macroOff[i] = target + smooth * (macroOff[i] - target);
            off += macroOff[i];
        }
        float v = base[i];
        if (off != 0.0f)
        {
            auto* p = params[i];
            v = p->convertFrom0to1 (clamp01 (p->convertTo0to1 (v) + off));
        }
        cur[i] = juce::jlimit (s.min, s.max, v);
    }
}

void SFProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& mainOut, juce::AudioBuffer<float>* sideAll,
                                int start, int n, Ctx& c, const juce::MidiBuffer& midi, bool panicNow, bool panicHold, bool lastChunk)
{
    // PANIC: once silent (after the 5 ms fade) and while held, the plugin takes no new input:
    // effects get silence, instruments no new notes. Nothing builds up behind the mute.
    if (panicNow) panicDown = true;
    const bool muteIn = panicPos <= 0 && (panicDown || panicHold);
    const int numMain = std::min (2, mainOut.getNumChannels());
    if (numMain == 0) return;

    // Views into the host buffers (AudioBuffer keeps up to 32 channel pointers inline: no allocation).
    float* mp[2] = { mainOut.getWritePointer (0, start), mainOut.getWritePointer (numMain - 1, start) };
    mainView.setDataToReferTo (mp, numMain, n);
    juce::AudioBuffer<float>* side = nullptr;
    if (sideAll != nullptr)
    {
        const int ns = std::min (2, sideAll->getNumChannels());
        float* sp[2] = { sideAll->getWritePointer (0, start), sideAll->getWritePointer (ns - 1, start) };
        sideView.setDataToReferTo (sp, ns, n);
        side = &sideView;
    }
    const int numExtra = numExtraOut;
    juce::AudioBuffer<float>* extra = nullptr;
    if (numExtra > 0)
    {
        float* ep[16] {};
        const int ne = std::min (16, 2 * numExtra);
        for (int ch = 0; ch < ne; ++ch) ep[ch] = extraScratch.getWritePointer (ch);
        extraView.setDataToReferTo (ep, ne, n);
        extraView.clear();
        extra = &extraView;
    }

    // This chunk's MIDI, offsets relative to the chunk (the last chunk takes any stragglers).
    chunkMidi.clear();
    float noteIn = 0.0f;
    for (auto it = midi.findNextSamplePosition (start); it != midi.cend(); ++it)
    {
        const auto m = *it;
        if (m.samplePosition >= start + n && ! lastChunk) break;
        const bool noteOn = m.numBytes >= 3 && (m.data[0] & 0xf0) == 0x90 && m.data[2] > 0;
        if (noteOn && muteIn) continue;
        chunkMidi.addEvent (m.data, m.numBytes, juce::jlimit (0, n - 1, m.samplePosition - start));
        if (noteOn) noteIn = std::max (noteIn, m.data[2] / 127.0f);
    }

    // MOTION input: effects follow their input level, instruments their note velocities.
    float motionIn = 0.0f;
    if (module->instrument()) motionIn = noteIn;
    else
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numMain; ++ch) pk = std::max (pk, mainView.getMagnitude (ch, 0, n));
        motionIn = clamp01 ((gainToDb (pk) + 48.0f) / 48.0f);
    }

    updateParams (c, n, motionIn);
    c.p = cur.data();
    c.p0 = prev.data();
    // SF BUILD's cut: whether this plugin takes part, and where it comes back. A note of its own up
    // to 30 ms before the way back brings it back 1 ms before that note: a part that plays a little
    // early keeps its attack.
    c.cutAt = cutA >= 0.0 && module->takesDropCut (c) ? cutA : -1.0;
    c.backAt = -1.0;
    if (c.cutAt >= 0.0 && cutB >= 0.0)
    {
        const double bpsC = c.beatsPerSample(), before = 0.030 * c.bpm / 60.0, ms = 0.001 * c.bpm / 60.0;
        for (const auto m : chunkMidi)
        {
            const bool on = m.numBytes >= 3 && (m.data[0] & 0xf0) == 0x90 && m.data[2] > 0;
            const double q = c.ppq + m.samplePosition * bpsC;
            if (on && q >= cutB - before && q < cutB) cutEarly = cutEarly < 0.0 ? q - ms : std::min (cutEarly, q - ms);
        }
        c.backAt = cutEarly >= 0.0 ? std::min (cutB, cutEarly) : cutB;
    }
    if (module->instrument()) mainView.clear();
    else if (muteIn) { mainView.clear(); panicInPos = 0; }
    else if (panicInPos < panicLen)
    {
        // After PANIC the input comes back over 5 ms too: an effect with latency (DELAY's dry
        // path, LIMIT's lookahead) would otherwise play a hard step after the output ramp ended.
        for (int ch = 0; ch < numMain; ++ch)
        {
            auto* x = mainView.getWritePointer (ch);
            for (int i = 0; i < n; ++i) x[i] *= (float) std::min (panicLen, panicInPos + i + 1) / (float) panicLen;
        }
        panicInPos = std::min (panicLen, panicInPos + n);
    }

    c.recallWaiting = pendingSong.load() >= 0;
    c.recalledThisBlock = recalledInBlock;
    c.lastChunk = lastChunk;
    module->process (c, mainView, side, extra, chunkMidi);

    // Safety: never pass NaN/Inf, never exceed +12 dBFS. A fault resets the module.
    bool bad = false;
    float peak = 0.0f;
    auto guard = [&] (juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* x = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (x[i])) { bad = true; x[i] = 0.0f; continue; }
                peak = std::max (peak, std::fabs (x[i]));
                x[i] = juce::jlimit (-4.0f, 4.0f, x[i]);
            }
        }
    };
    guard (mainView);
    if (extra != nullptr) guard (*extra);
    // Held at the clamp for 250 ms = runaway (feedback, broken state): reset.
    if (peak >= 3.99f) overSamples += n;
    else if (peak < 2.0f) overSamples = 0;
    const bool runaway = overSamples > (int) (0.25 * sr);
    if (bad || runaway)
    {
        mainView.clear();
        if (extra != nullptr) extra->clear();
        module->reset();
        overSamples = 0;
        fault = true;
        faultCount.fetch_add (1);
    }
    else
    {
        // SF BUILD's cut: down over 3 ms, back up over 0.25 ms (as the old GAP), by the song position
        // of the music each sample carries (this plugin's own less its latency).
        if (c.cutAt >= 0.0 || cutGain < 1.0f)
        {
            const double bpsC = c.beatsPerSample(), lat = (double) module->latency();
            const float down = 1.0f / (0.003f * (float) sr), up = 1.0f / (0.00025f * (float) sr);
            for (int i = 0; i < n; ++i)
            {
                cutGain = c.inCut (c.ppq + ((double) i - lat) * bpsC) ? std::max (0.0f, cutGain - down) : std::min (1.0f, cutGain + up);
                if (cutGain >= 1.0f) continue;
                for (int ch = 0; ch < mainView.getNumChannels(); ++ch) mainView.getWritePointer (ch)[i] *= cutGain;
                if (extra != nullptr)
                    for (int ch = 0; ch < extra->getNumChannels(); ++ch) extra->getWritePointer (ch)[i] *= cutGain;
            }
        }
        // PANIC: fade out over 5 ms (however the chunks fall; a second press mid-fade carries
        // on), then clear every voice, tail and delay line. Silent while held; fades back in
        // over 5 ms on release.
        // (Integer sample counts: the ramp is the same however the chunks fall.)
        const int target = (panicDown || panicHold) ? 0 : panicLen;
        if (panicPos != target)
        {
            const int dir = target > panicPos ? 1 : -1;
            auto ramp = [&] (juce::AudioBuffer<float>& b)
            {
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                {
                    auto* x = b.getWritePointer (ch);
                    for (int i = 0; i < n; ++i) x[i] *= (float) juce::jlimit (0, panicLen, panicPos + dir * (i + 1)) / (float) panicLen;
                }
            };
            ramp (mainView);
            if (extra != nullptr) ramp (*extra);
            panicPos = juce::jlimit (0, panicLen, panicPos + dir * n);
        }
        else if (panicPos <= 0)
        {
            mainView.clear();
            if (extra != nullptr) extra->clear();
        }
        if (panicDown && panicPos <= 0) { module->reset(); panicDown = false; }
    }

    if (extra != nullptr)
        for (int e = 0; e < numExtra; ++e)
            if (getBusCount (false) > e + 1 && getBus (false, e + 1)->isEnabled())
            {
                auto out = getBusBuffer (buffer, false, e + 1);
                for (int ch = 0; ch < std::min (2, out.getNumChannels()); ++ch)
                    out.copyFrom (ch, start, *extra, e * 2 + ch, 0, n);
            }
}

void SFProcessor::timerCallback() { flushHostNotifications(); serviceResample(); }

// ------------------------------------------------------------------------------ RESAMPLE
juce::File SFProcessor::resampleFolder()
{
    if (testResampleFolder != juce::File()) return testResampleFolder;
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SF Live Suite").getChildFile ("Resamples");
}

void SFProcessor::armResample (int bars)
{
    if (! module->acceptsFiles() || resampleState.load() != 0) return;
    // The buffer made here, never on the audio thread: room for the bars at 40 BPM (at most 30 s).
    // The length is fixed at the first block after this, at that block's tempo.
    resampleBars = juce::jlimit (1, 4, bars);
    resampleBuf.setSize (2, std::max (1, (int) std::lround (std::min (30.0, resampleBars * std::max (4.0, lastBpb) * 60.0 / 40.0) * getSampleRate())), false, true, false);
    resampleLen = 0;
    resamplePos = 0;
    resampleAt = -1;
    resampleState.store (1, std::memory_order_release);
}

// The audio thread, after the block: wait for the bar line (as late as the latency, so the take
// starts with the bar's sound: where, in the stream's samples, is fixed at the first block after
// arming), then copy the output until the buffer is full.
void SFProcessor::recordResample (const juce::AudioBuffer<float>& out, int n, int64_t blockStart, double ppq0, double bps, bool playing, double beatsPerBar)
{
    int from = 0;
    if (resampleState.load (std::memory_order_acquire) == 1)
    {
        if (resampleAt < 0)
        {
            const double bar = std::max (0.5, beatsPerBar);
            const double next = barPhase + std::ceil ((ppq0 - barPhase) / bar - 1e-9) * bar;
            resampleAt = playing ? blockStart + (int64_t) std::ceil ((next - ppq0) / bps - 1e-9) + getLatencySamples() : blockStart;
            resampleLen = std::min (resampleBuf.getNumSamples(), std::max (1, (int) std::lround (resampleBars * bar / bps)));
        }
        if (resampleAt >= blockStart + n) return;
        from = (int) std::max<int64_t> (0, resampleAt - blockStart);
        resampleState.store (2, std::memory_order_release);
    }
    const int k = std::min (n - from, resampleLen - resamplePos);
    for (int ch = 0; ch < 2; ++ch)
        resampleBuf.copyFrom (ch, resamplePos, out, std::min (ch, out.getNumChannels() - 1), from, k);
    resamplePos += k;
    if (resamplePos >= resampleLen) resampleState.store (3, std::memory_order_release);
}

void SFProcessor::serviceResample()
{
    if (resampleState.load (std::memory_order_acquire) != 3) return;
    auto folder = resampleFolder();
    folder.createDirectory();
    const auto name = "SF " + juce::String (module->id()) + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    auto file = folder.getNonexistentChildFile (name, ".wav", false);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), getSampleRate(), 2, 24, {}, 0));
        if (w != nullptr) w->writeFromAudioSampleBuffer (resampleBuf, 0, resampleLen);
    }
    resampleState.store (0, std::memory_order_release);
    resampleBuf.setSize (2, 0);
    lastResample = file;
    if (file.existsAsFile()) module->loadFiles (juce::StringArray (file.getFullPathName()));
}

void SFProcessor::updateTrackProperties (const TrackProperties& p)
{
    // Live (VST3) tells the plugin its track name; ALLOC lists sources by it, and GLIDE RINGS finds
    // the plugin's E16 knobs by it. Live reports the name and the colour in separate calls, and a
    // call without the name arrives here with an empty one: it must not wipe the name (a track in
    // Live always has one).
    if (p.name.trim().isEmpty()) return;
    char buf[24] {};
    p.name.toUpperCase().copyToUTF8 (buf, 23);
    std::memcpy (trackName, buf, sizeof (buf));
    trackNameSeq.fetch_add (1);
}

// ------------------------------------------------------------------------------ presets
void SFProcessor::applyPreset (const Preset& pr)
{
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const auto& s = specs[i];
        if (! s.preset || s.type == PType::Momentary) continue;
        float v = s.def;
        for (auto& kv : pr.values) if (kv.first == s.id) { v = kv.second; break; }
        // Chosen now: newer than a recall Live hasn't been told of yet, even at the value Live held
        // before it (as a knob moved away and back is).
        departGen[i] = recallGen[i].load();
        auto* p = params[i];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (s.min, s.max, v)));
        p->endChangeGesture();
    }
    currentPreset = pr.name;
}

// ------------------------------------------------------------------------------ randomiser
bool SFProcessor::randomisable (int i) const
{
    // Sound modules only (the ones with factory presets): CONTROL, DECK, ALLOC, TAP and ROOM EQ are
    // how the set runs (a random KEY or SONG on CONTROL would move the whole set).
    if (i < 0 || i >= numModParams || factory.empty()) return false;
    const auto& s = specs[(size_t) i];
    return ! s.hidden && s.preset && ! s.setup && ! s.inSample && (s.type == PType::Float || s.type == PType::Choice);
}

void SFProcessor::defaultLocks()
{
    static const char* const ids[] = { "level", "filter", "density", "mix", "tune", "shift", "acid", "warp", "energy" };
    for (size_t i = 0; i < specs.size() && i < (size_t) kMaxParams; ++i)
        locked[i] = std::any_of (std::begin (ids), std::end (ids), [&] (const char* id) { return specs[i].id == id; });
}

void SFProcessor::randomise (bool nudge, uint32_t seed)
{
    sf::Noise rng { seed | 1u };
    for (int i = 0; i < numModParams; ++i)
    {
        if (! randomisable (i) || isLocked (i)) continue;
        const auto& s = specs[(size_t) i];
        if (nudge && s.type != PType::Float) continue;
        auto* p = params[(size_t) i];
        float v;
        if (s.type == PType::Choice)
        {
            const int n = (int) s.choices.size();
            v = p->convertTo0to1 ((float) juce::jlimit (0, n - 1, (int) (rng.uni() * (float) n)));
        }
        else v = nudge ? juce::jlimit (0.0f, 1.0f, p->getValue() + 0.15f * rng.next()) : rng.uni();
        departGen[(size_t) i] = recallGen[(size_t) i].load();
        p->beginChangeGesture();
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    }
    currentPreset = {};
}

Preset SFProcessor::capturePreset (const juce::String& name) const
{
    Preset p;
    p.name = name.toStdString();
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].preset && specs[i].type != PType::Momentary)
            p.values.push_back ({ specs[i].id, raw[i]->load() });
    return p;
}

juce::File SFProcessor::userPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("SF Live Suite").getChildFile ("Presets").getChildFile (module->id());
}

juce::Array<juce::File> SFProcessor::userPresets() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.sfpreset");
    files.sort();
    return files;
}

bool SFProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim()).toUpperCase();
    if (clean.isEmpty()) return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory()) return false;
    const auto pr = capturePreset (clean);
    juce::ValueTree t ("SFPRESET");
    t.setProperty ("module", juce::String (module->id()), nullptr);
    t.setProperty ("name", clean, nullptr);
    t.setProperty ("version", 1, nullptr);
    for (auto& kv : pr.values)
    {
        juce::ValueTree v ("PARAM");
        v.setProperty ("id", juce::String (kv.first), nullptr);
        v.setProperty ("value", kv.second, nullptr);
        t.appendChild (v, nullptr);
    }
    auto xml = t.createXml();
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + ".sfpreset"))) return false;
    currentPreset = clean;
    return true;
}

bool SFProcessor::loadUserPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("SFPRESET") || t.getProperty ("module").toString() != juce::String (module->id())) return false;
    Preset p;
    p.name = t.getProperty ("name", f.getFileNameWithoutExtension()).toString().toStdString();
    for (auto v : t)
        if (v.hasType ("PARAM")) p.values.push_back ({ v.getProperty ("id").toString().toStdString(), (float) v.getProperty ("value") });
    applyPreset (p);
    return true;
}

// ------------------------------------------------------------------------------ state
void SFProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    juce::ValueTree root ("SFSTATE");
    root.setProperty ("module", juce::String (module->id()), nullptr);
    root.setProperty ("version", 2, nullptr);
    root.setProperty ("motionSeed", (int) motionSeed, nullptr);
    root.setProperty ("uid", juce::String::toHexString ((juce::int64) instanceUid.load()), nullptr);
    root.setProperty ("preset", currentPreset, nullptr);
    if (const int f = uiFold.load(); f != 0) root.setProperty ("ui", f, nullptr);   // only when not the default
    {
        // The randomiser's locks, by id (only the randomisable controls; "-" for none locked).
        juce::StringArray ids;
        for (int i = 0; i < numModParams; ++i) if (randomisable (i) && isLocked (i)) ids.add (specs[(size_t) i].id);
        root.setProperty ("locks", ids.isEmpty() ? juce::String ("-") : ids.joinIntoString (","), nullptr);
    }
    // The settings in force, as the sound and STORE have them: a value just recalled that Live
    // hasn't been told yet is the setting (Live still holds the one from before the recall), unless
    // a newer change in Live superseded it. Targets, not where a glide, MACRO or MOTION has them.
    auto params = apvts.copyState();
    for (auto row : params)
    {
        const int i = paramIndex (row.getProperty ("id").toString().toStdString());
        if (i >= 0 && overrideInForce ((size_t) i)) row.setProperty ("value", ovr[(size_t) i].load(), nullptr);
    }
    root.appendChild (params, nullptr);
    juce::ValueTree songs ("SONGS");
    songs.setProperty ("active", songIndex.load(), nullptr);
    // The songs whole (songLock: a STORE on the audio thread waits a block meanwhile).
    std::array<std::array<float, kMaxParams>, kSongs> vals;
    std::array<bool, kSongs> has {};
    lockSongs();
    vals = songVals;
    for (int s = 0; s < kSongs; ++s) has[(size_t) s] = songHas[(size_t) s].load();
    songLock.store (false, std::memory_order_release);
    for (int s = 0; s < kSongs; ++s)
    {
        if (! has[(size_t) s]) continue;
        juce::ValueTree t ("SONG");
        t.setProperty ("index", s, nullptr);
        for (size_t i = 0; i < specs.size(); ++i)
            if (specs[i].song) t.setProperty (juce::Identifier (specs[i].id), vals[(size_t) s][i], nullptr);
        songs.appendChild (t, nullptr);
    }
    root.appendChild (songs, nullptr);
    juce::ValueTree m ("MODULE");
    module->save (m);
    root.appendChild (m, nullptr);
    juce::MemoryOutputStream os (mb, false);
    root.writeToStream (os);
}

void SFProcessor::setStateInformation (const void* data, int size)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) size);
    if (! root.isValid() || root.getType().toString() != "SFSTATE") return;
    // Newer than everything asked for before it (stateGen, odd from here to the end): the recall
    // in force gives way and is never reported, whatever the state's values; a song waiting for
    // its bar line and a STORE not yet made are over (the audio thread ends them itself). Odd under
    // songLock: a STORE is made wholly before the load, or not at all.
    lockSongs();
    stateGen.fetch_add (1);
    songLock.store (false, std::memory_order_release);
    auto p = root.getChildWithName ("PARAMS");
    if (p.isValid())
    {
        // Momentary buttons (PANIC, STORE ALL, THROW ...) never come back pressed: a set saved
        // with PANIC held would otherwise open silent.
        auto pc = p.createCopy();
        for (auto child : pc)
        {
            const auto id = child.getProperty ("id").toString().toStdString();
            for (const auto& s : specs) if (s.type == PType::Momentary && s.id == id) child.setProperty ("value", 0.0f, nullptr);
        }
        apvts.replaceState (pc);
    }
    if (root.hasProperty ("motionSeed"))
    {
        motionSeed = (uint32_t) (int) root.getProperty ("motionSeed");
        for (int k = 0; k < kMotions; ++k) motion[(size_t) k].setSeed (motionSeed + 0x9E3779B9u * (uint32_t) k);
    }
    if (root.hasProperty ("uid"))
        if (const auto u = (uint64_t) root.getProperty ("uid").toString().getHexValue64(); u != 0) instanceUid = u;
    currentPreset = root.getProperty ("preset", "").toString();
    uiFold = (int) root.getProperty ("ui", 0);
    // The locks as saved; a set from before the randomiser gets the first ones.
    if (root.hasProperty ("locks"))
    {
        const auto ids = juce::StringArray::fromTokens (root.getProperty ("locks").toString(), ",", "");
        for (int i = 0; i < numModParams; ++i) setLocked (i, ids.contains (juce::String (specs[(size_t) i].id)));
    }
    else defaultLocks();
    auto songs = root.getChildWithName ("SONGS");
    lockSongs();
    for (auto& h : songHas) h = false;
    if (songs.isValid())
    {
        songIndex = (int) songs.getProperty ("active", -1);
        for (auto t : songs)
        {
            const int s = (int) t.getProperty ("index", -1);
            if (s < 0 || s >= kSongs) continue;
            for (size_t i = 0; i < specs.size(); ++i)
                songVals[(size_t) s][i] = t.hasProperty (juce::Identifier (specs[i].id)) ? (float) t.getProperty (juce::Identifier (specs[i].id)) : specs[i].def;
            songHas[(size_t) s] = true;
        }
    }
    songLock.store (false, std::memory_order_release);
    auto m = root.getChildWithName ("MODULE");
    if (m.isValid()) module->load (m);
    module->restored();
    // In: SF CONTROL's requests so far are older than this state; the audio thread starts the
    // knobs where it has them (no glide) at its next block.
    const auto& d = sf::Bus::get().d();
    songSeqAtLoad = d.songSeq.load();
    storeSeqAtLoad = d.storeSeq.load();
    stateGen.fetch_add (1);
}

// Not the audio thread: the songs to this thread alone (the audio thread holds them a moment at most).
void SFProcessor::lockSongs()
{
    while (songLock.exchange (true, std::memory_order_acquire)) std::this_thread::yield();
}

juce::AudioProcessorEditor* SFProcessor::createEditor() { return new SFEditor (*this); }
