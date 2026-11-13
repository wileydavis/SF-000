#include "Session.h"
#include "Build.h"
#include <juce_cryptography/juce_cryptography.h>

namespace sf0
{
using juce::var;

static const int kMorphEnd = cx::kRetMS;      // lanes, returns and the drum bus: what a snapshot knob can reach

Session::Session (Engine& e, bool disk) : engine (e), useDisk (disk)
{
    genType.fill (-1);
    for (auto& a : movedAt) a = 0;
    for (auto& b : movedBy) b = 0;
    base.resize ((size_t) cx::kCount);
    offset.assign ((size_t) cx::kCount, 0.0f);
    for (int i = 0; i < cx::kCount; ++i) base[(size_t) i] = engine.target (i);
    for (int r = 0; r < 2; ++r) retInit[(size_t) r] = engine.getSetup ({ 2, 0, r });
    busInit = engine.getSetup ({ 3, 0, 0 });
    if (useDisk) engine.keepFile = [this] (const juce::String& path) { return keepSample (path); };       // every audio file the rig is given: the library's own copy of it
    if (! (useDisk && load())) { makeFactory(); dirty = true; }
    ensureCleanSlate();
    findLibraryId (idInFile);
    sign();
    if (useDisk) startTimer (3000);
}

Session::~Session()
{
    stopTimer();
    finishTakes (true);
    engine.dropCue();                   // before the clips go
    waitForClips();
    if (useDisk) { save(); engine.keepFile = nullptr; }
}

void Session::timerCallback()
{
    if (printRig) return;                           // (a print's own rig: the print does its looking)
    // The arrangement, while it plays: looked at often, for the part that is next. The rest of
    // what this does is slower work, done about once a second meanwhile.
    if (arrOn) { arrTick(); if (arrOn && ++arrTicks % 64 != 0) return; }
    finishTakes();
    engine.tidy();
    pullHeard();
    if (samplesMoved) { samplesMoved = false; sweepSamples(); }      // a sample was given: the one it replaced, if nothing holds it now, goes
    // (the timer runs faster while a snapshot waits for its sound; the file is still written no more than every three seconds)
    if (const auto now = juce::Time::getMillisecondCounter(); useDisk && dirty && now - savedAt >= 2900) { savedAt = now; save(); }
    pruneCues();
    if (arrOn) { if (useDisk) startTimer (15); }
    else if (waiting.empty()) { if (useDisk) startTimer (3000); else stopTimer(); }
}

double Session::glideBeats() const { return glideBarsFor (engine.target (cx::kGlide)) * 4.0; }

// ------------------------------------------------------------------------------------- controls
void Session::selectLane (int l)
{
    l = juce::jlimit (0, kLanes - 1, l);
    if (l == activeLane) return;
    bringLane (l, 2);
}

// ------------------------------------------------------------------------------ what is in front
void Session::bringLane (int l, int by)
{
    l = juce::jlimit (0, kLanes - 1, l);
    if (l != activeLane) { activeLane = l; selStep = -1; page = 0; }
    front = { fLane, 0 };
    // CONFIG keeps to the lane: its instrument's, or the same slot's of this lane's inserts. (Open
    // on a return it stays there under the mouse; a controller's or the keyboard's lane takes it.)
    if (cfgOpen && cfg.kind < 2) cfg.lane = l; else if (cfgOpen && by != 0) cfg = { 0, l, 0 };
    frontAt = juce::Time::getMillisecondCounter(); frontBy = by; ++frontCount;
    touch();
    if (onChange) onChange();
}

void Session::bring (Front f, int by)
{
    if (f.kind == fLane) { bringLane (activeLane, by); return; }
    f.slot = f.kind == fInsert ? juce::jlimit (0, kSlots - 1, f.slot) : 0;
    front = f;
    if (cfgOpen && by != 0)                     // (under the mouse CONFIG stays where it was put)
    {
        if (f.kind == fInsert) cfg = { 1, activeLane, f.slot };
        else if (f.kind == fReturns) { if (cfg.kind != 2) cfg = { 2, 0, 0 }; }
        else if (f.kind == fMaster) cfg = { 3, 0, 0 };          // (the master's CONFIG is its drum bus's)
        else cfgOpen = false;                   // (the sequencer strip, the snapshot knobs: no CONFIG)
    }
    frontAt = juce::Time::getMillisecondCounter(); frontBy = by; ++frontCount;
    if (onChange) onChange();
}

// A lane, then its inserts that have an effect in them, then the next lane; then the snapshot
// knobs, the returns, the master, the sequencer strip.
std::vector<Session::Stop> Session::stops() const
{
    std::vector<Stop> v;
    for (int l = 0; l < kLanes; ++l)
    {
        v.push_back ({ fLane, l, 0 });
        for (int sl = 0; sl < kSlots; ++sl) if (engine.insertType (l, sl) >= 0) v.push_back ({ fInsert, l, sl });
    }
    for (int k : { (int) fSnap, (int) fReturns, (int) fMaster, (int) fSeq }) v.push_back ({ k, 0, 0 });
    return v;
}

Session::Stop Session::stopNow() const
{
    return { front.kind, front.kind == fLane || front.kind == fInsert ? activeLane : 0, front.kind == fInsert ? front.slot : 0 };
}

int Session::stopIndex (const std::vector<Stop>& v, Stop s)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return (int) i;
    if (s.kind == fInsert) return stopIndex (v, { fLane, s.lane, 0 });      // (an insert with nothing in it is no stop: its lane is)
    return -1;
}

int Session::placeOf (Stop s)
{
    static_assert (kLanes == 11, "the panels are laid out for eleven lanes");
    switch (s.kind)
    {
        case fLane:    return s.lane < 5 ? s.lane : s.lane + 1;
        case fSnap:    return 5;
        case fInsert:  return 12 + juce::jlimit (0, kSlots - 1, s.slot);
        case fReturns: return 15;
        case fMaster:  return 16;
        default:       return 17;
    }
}

void Session::bringStop (Stop s, int by)
{
    if (s.kind == fLane) { bringLane (s.lane, by); return; }
    if (s.kind == fInsert)
        if (const int l = juce::jlimit (0, kLanes - 1, s.lane); l != activeLane)
        {
            // (an insert of another lane: that lane is the active one, and its insert is what is in front)
            activeLane = l; selStep = -1; page = 0;
            if (cfgOpen && cfg.kind < 2) cfg.lane = l;
            touch();
        }
    bring ({ s.kind, s.slot }, by);
}

juce::String Session::stopName (Stop s) const
{
    switch (s.kind)
    {
        case fLane:    return juce::String (s.lane + 1) + " " + lanes()[(size_t) juce::jlimit (0, kLanes - 1, s.lane)].name;
        case fInsert:  { const int t = engine.insertType (s.lane, s.slot); return juce::String (s.lane + 1) + "." + juce::String (s.slot + 1) + " " + (t >= 0 ? effects()[(size_t) t].name : "EMPTY"); }
        case fSnap:    return "SNAPSHOTS";
        case fReturns: return "RETURNS";
        case fMaster:  return "MASTER";
        default:       return "SEQUENCER";
    }
}

void Session::point (Stop s)
{
    if (pointing && pointAt == s) return;
    pointing = true; pointAt = s;
    if (onChange) onChange();
}

void Session::pointOff()
{
    if (! pointing) return;
    pointing = false;
    if (onChange) onChange();
}

// ------------------------------------------------------------------------------------- CONFIG
void Session::openConfig (Engine::Target t)
{
    const bool same = cfgOpen && cfg.kind == t.kind && cfg.slot == t.slot && (t.kind >= 2 || cfg.lane == t.lane);
    if (same) { cfgOpen = false; if (onChange) onChange(); return; }
    cfg = t; cfgOpen = true; cfgTouched = {};
    if (t.kind == 0 && activeLane != t.lane) { activeLane = t.lane; selStep = -1; page = 0; }
    if (onChange) onChange();
}

bool Session::configOfFront (int by)
{
    frontAt = juce::Time::getMillisecondCounter(); frontBy = by; ++frontCount;
    switch (front.kind)
    {
        case fLane:    openConfig ({ 0, activeLane, 0 }); return true;
        case fInsert:  openConfig ({ 1, activeLane, front.slot }); return true;
        case fReturns: openConfig ({ 2, 0, cfgOpen && cfg.kind == 2 && cfg.slot == 0 ? 1 : cfgOpen && cfg.kind == 2 ? cfg.slot : 0 }); return true;       // A; then B; then (B again) shut
        case fMaster:  openConfig ({ 3, 0, 0 }); return true;                                  // (its drum bus)
        default:       if (cfgOpen) { cfgOpen = false; if (onChange) onChange(); } return false;
    }
}

// ---- POCKETS as a controller's CONFIG pages have it
int Session::pocketControl (int index)
{
    if (index < 0 || index >= kPocketList) return -1;
    static const int laneKnob[4] = { 0, 1, 3, 2 }, rigKnob[4] = { 0, 1, 2, 3 };        // HOME, WIDTH, YIELD, HOLD; AMOUNT, CLEAR, SPEED, RETURNS
    const int page = index / (kLanes + 1), place = index % (kLanes + 1);
    return place < kLanes ? cx::pk (place, laneKnob[page]) : cx::kPkG + rigKnob[page];
}

const char* Session::pocketPageName (int page)
{
    static const char* const names[4] = { "HOME", "WIDTH", "YIELD", "HOLD" };
    return names[juce::jlimit (0, 3, page)];
}

int Session::pocketsAuto()
{
    Pockets::Seen seen;
    engine.pocketsSeen (seen);
    Pockets::Set out;
    std::array<bool, kLanes> placed;
    Pockets::place (seen, engine.pocketSet (false), out, placed);
    int n = 0;
    for (int l = 0; l < kLanes; ++l)
        if (placed[(size_t) l]) { userSet (cx::pk (l, 0), out.home[(size_t) l]); userSet (cx::pk (l, 1), out.width[(size_t) l]); ++n; }
    if (onChange) onChange();
    return n;
}

void Session::pocketsReset()
{
    for (int l = 0; l < kLanes; ++l)
        for (int k = 0; k < kPkLane; ++k) userSet (cx::pk (l, k), engine.control (cx::pk (l, k)).def);
    if (onChange) onChange();
}

std::vector<Engine::SetupParam> Session::configList() const
{
    if (pocketsUp())
    {
        std::vector<Engine::SetupParam> list;
        for (int i = 0; i < kPocketList; ++i)
        {
            const int ctl = pocketControl (i), place = i % (kLanes + 1);
            const auto& c = engine.control (ctl);
            Engine::SetupParam sp;
            sp.id = c.id; sp.kind = c.kind; sp.value = engine.target (ctl); sp.def = c.def; sp.text = engine.valueText (ctl);
            sp.name = place < kLanes ? juce::String (lanes()[(size_t) place].name) : c.label;      // (on a page of HOMEs, a knob is known by its lane)
            sp.group = pocketPageName (i / (kLanes + 1));
            list.push_back (sp);
        }
        return list;
    }
    if (macrosUp())
    {
        std::vector<Engine::SetupParam> list;
        for (int ctl = cx::kMot; ctl < cx::kSnap; ++ctl)
        {
            const auto& c = engine.control (ctl);
            Engine::SetupParam sp;
            sp.id = c.id; sp.name = c.label; sp.kind = c.kind; sp.choices = c.choices; sp.value = engine.target (ctl); sp.def = c.def; sp.text = engine.valueText (ctl);
            sp.group = "MOTION " + juce::String ((ctl - cx::kMot) / cx::kMotKnobs + 1);
            list.push_back (sp);
        }
        return list;
    }
    return cfgOpen && engine.processorFor (cfg) != nullptr ? engine.setupParams (cfg) : std::vector<Engine::SetupParam>();
}

void Session::setConfig (int index, int v7)
{
    if (pocketsUp())
    {
        // (POCKETS' are the rig's own knobs: turned as one on the screen is, and they travel)
        if (const int ctl = pocketControl (index); ctl >= 0)
        {
            const float v = (float) juce::jlimit (0, 127, v7) / 127.0f;
            if (std::abs (v - engine.target (ctl)) < 1.0e-6f) return;
            moved (ctl, 1);
            userSet (ctl, v);
            if (onChange) onChange();
        }
        return;
    }
    const auto list = configList();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& sp = list[(size_t) index];
    v7 = juce::jlimit (0, 127, v7);
    float v = (float) v7 / 127.0f;
    if (sp.kind == Kind::Hold) return;                                  // (a key that is held: not a knob's to turn)
    if (sp.kind == Kind::Toggle) v = v7 >= 64 ? 1.0f : 0.0f;
    else if (sp.kind == Kind::List && sp.choices.size() > 1) v = (float) juce::jmin (sp.choices.size() - 1, v7 * sp.choices.size() / 128) / (float) (sp.choices.size() - 1);
    else if (sp.kind == Kind::Bipolar) v = v7 <= 64 ? (float) v7 / 128.0f : 0.5f + (float) (v7 - 64) / 126.0f;
    if (std::abs (v - sp.value) < 1.0e-6f) return;
    if (macrosUp())
    {
        // (MOTION's are the rig's own knobs: turned as one on the screen is)
        moved (cx::kMot + index, 1);
        userSet (cx::kMot + index, v);
        if (onChange) onChange();
        return;
    }
    engine.setSetup (cfg, sp.id, v);
    cfgTouched = sp.id;
    touch();
    if (onChange) onChange();
}

void Session::userSet (int i, float v, bool sounding)
{
    if (i < 0 || i >= cx::kCount) return;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (i >= cx::kSnap) { turnSnapKnob (i - cx::kSnap, v); return; }
    const auto& c = engine.control (i);
    base[(size_t) i] = c.morph && i < kMorphEnd ? v - offset[(size_t) i] : v;
    // a sound knob under the hand travels over the GLIDE time, as a recalled one does (OFF: at once)
    if (! sounding) engine.set (i, v, c.glides ? glideBeats() : 0.0);
    if (cx::isMot (i)) motionLaws();        // (MOTION put on a knob, or taken off: that knob's law is wanted, or no longer)
    if (i < cx::kRet)
    {
        // a generator knob rewrites the pattern while its generator is armed
        const int l = i / cx::kPerLane, k = i % cx::kPerLane;
        if (k >= cx::kGen && genType[(size_t) l] >= 0) regenerate (l);
    }
    touch();
}

void Session::turnSnapKnob (int k, float v)
{
    if (k != learning) engine.set (cx::kSnap + k, v);       // (the knob being taught stands at its full turn)
    applyMorph (glideBeats());
    touch();
}

bool Session::reaches (const Engine& e, int ctl) { return ctl >= 0 && ctl < kMorphEnd && e.control (ctl).morph; }

bool Session::knobLive (int k) const
{
    if (k < 0 || k >= 16) return false;
    const auto& kn = knobs[(size_t) k];
    if (! kn.links.empty() || k == learning) return true;
    int held = 0;
    for (auto id : kn.snaps) if (findSnap (id) != nullptr) ++held;
    return held >= 2;
}

void Session::applyMorph (double glide)
{
    std::fill (offset.begin(), offset.end(), 0.0f);
    for (int k = 0; k < 16; ++k)
    {
        const auto& kn = knobs[(size_t) k];
        const float v = engine.target (cx::kSnap + k);
        if (v <= 0.0f) continue;
        // the knobs it is tied to: each its own share of the turn
        for (const auto& l : kn.links) if (reaches (engine, l.ctl)) offset[(size_t) l.ctl] += kn.depth * l.amount * v;
        std::vector<const Snapshot*> ss;
        for (auto id : kn.snaps) if (auto* s = findSnap (id)) ss.push_back (s);
        if (ss.size() < 2) continue;
        const float p = v * (float) (ss.size() - 1);
        const int a = juce::jmin ((int) ss.size() - 2, (int) p);
        const float f = p - (float) a;
        for (int i = 0; i < kMorphEnd; ++i)
        {
            if (! engine.control (i).morph) continue;
            const float s0 = ss[0]->values[(size_t) i], sa = ss[(size_t) a]->values[(size_t) i], sb = ss[(size_t) a + 1]->values[(size_t) i];
            if (s0 < 0.0f || sa < 0.0f || sb < 0.0f) continue;
            offset[(size_t) i] += kn.depth * (sa + (sb - sa) * f - s0);
        }
    }
    for (int i = 0; i < kMorphEnd; ++i)
    {
        if (! engine.control (i).morph) continue;
        const float t = juce::jlimit (0.0f, 1.0f, base[(size_t) i] + offset[(size_t) i]);
        if (std::abs (t - engine.target (i)) > 1e-5f) engine.set (i, t, glide);
    }
    motionLaws();
}

int Session::motionOn (int k) const
{
    int bits = 0;
    for (int u = 0; u < cx::kMotUnits; ++u)
    {
        const auto& w = engine.control (cx::mot (u, 0));
        const bool wave = (int) std::lround (engine.target (cx::mot (u, 0)) * (float) (w.steps() - 1)) != 0;
        const bool amount = std::abs (engine.target (cx::mot (u, 3)) - 0.5f) > 1.0e-4f;
        if (wave && amount && (int) std::lround (engine.target (cx::mot (u, 2)) * 15.0f) == k) bits |= 1 << u;
    }
    return bits;
}

// What applyMorph works out for where a knob stands, written as a law for every place it could
// stand: for each control the knob moves, how far each of its snapshots has it from the first,
// and the slope of a knob it is tied to. The audio thread reads these to swing the knob itself.
void Session::motionLaws()
{
    std::array<bool, 16> on {};
    bool any = false;
    for (int k = 0; k < 16; ++k) { on[(size_t) k] = motionOn (k) != 0 && knobLive (k); any = any || on[(size_t) k]; }
    // has anything a law is made from changed?
    juce::uint64 sig = any ? 1469598103934665603ull : 0ull;
    auto mix = [&sig] (juce::uint64 v) { sig = (sig ^ v) * 1099511628211ull; };
    auto mixF = [&mix] (float f) { juce::uint32 u; std::memcpy (&u, &f, 4); mix (u); };
    for (int k = 0; any && k < 16; ++k)
    {
        if (! on[(size_t) k]) continue;
        const auto& kn = knobs[(size_t) k];
        mix ((juce::uint64) k + 1); mixF (kn.depth);
        for (auto id : kn.snaps) { mix ((juce::uint64) id); mix (findSnap (id) != nullptr ? 2u : 3u); }
        mix (0xABCDu);
        for (const auto& l : kn.links) { mix ((juce::uint64) (l.ctl + 7)); mixF (l.amount); }
    }
    if (sig == lawsSig) return;
    lawsSig = sig;
    if (! any) { engine.setKnobLaws (nullptr); return; }
    auto laws = std::make_unique<Engine::KnobLaws>();
    for (int k = 0; k < 16; ++k)
    {
        if (! on[(size_t) k]) continue;
        const auto& kn = knobs[(size_t) k];
        auto& law = laws->knob[(size_t) k];
        std::vector<const Snapshot*> ss;
        for (auto id : kn.snaps) if (auto* sn = findSnap (id)) ss.push_back (sn);
        if (ss.size() > (size_t) Engine::KnobLaw::kNodes) ss.resize ((size_t) Engine::KnobLaw::kNodes);
        law.nodes = ss.size() >= 2 ? (int) ss.size() : 0;
        for (int i = 0; i < kMorphEnd; ++i)
        {
            if (! engine.control (i).morph) continue;
            Engine::KnobLaw::Row r;
            r.ctl = i;
            for (const auto& l : kn.links) if (l.ctl == i) r.link += kn.depth * l.amount;
            bool moves = r.link != 0.0f;
            for (int j = 0; j < law.nodes; ++j)
            {
                const float s0 = ss[0]->values[(size_t) i], sj = ss[(size_t) j]->values[(size_t) i];
                r.node[(size_t) j] = s0 < 0.0f || sj < 0.0f ? Engine::KnobLaw::kNone : kn.depth * (sj - s0);
                moves = moves || (r.node[(size_t) j] != 0.0f && r.node[(size_t) j] < Engine::KnobLaw::kNone * 0.5f);
            }
            if (moves) law.rows.push_back (r);
        }
    }
    engine.setKnobLaws (std::move (laws));
}

void Session::pressSnapKnob (int k)
{
    if (k == learning) return;
    const auto& kn = knobs[(size_t) k];
    std::vector<juce::int64> ids;
    for (auto id : kn.snaps) if (findSnap (id) != nullptr) ids.push_back (id);
    if (ids.empty())
    {
        // Tied to knobs and nothing else: there is no snapshot to land on, so it lands on the
        // nearer end of its own turn. Past half way what it would push at full is where those
        // knobs stand from now on; short of it they go back. Either way the knob goes home.
        if (kn.links.empty()) return;
        if (engine.target (cx::kSnap + k) >= 0.5f)
            for (const auto& l : kn.links)
                if (reaches (engine, l.ctl)) base[(size_t) l.ctl] = juce::jlimit (0.0f, 1.0f, base[(size_t) l.ctl] + kn.depth * l.amount);
        engine.set (cx::kSnap + k, 0.0f);
        applyMorph (glideBeats());
        touch();
        if (onChange) onChange();
        return;
    }
    const float v = engine.target (cx::kSnap + k);
    const auto id = ids[(size_t) juce::jlimit (0, (int) ids.size() - 1, (int) std::lround (v * (float) (ids.size() - 1)))];
    engine.set (cx::kSnap + k, 0.0f);
    recall (id);
}

bool Session::recallFromKnob (int k, int n)
{
    if (k < 0 || k >= 16) return false;
    std::vector<juce::int64> ids;
    for (auto id : knobs[(size_t) k].snaps) if (findSnap (id) != nullptr) ids.push_back (id);
    if (n < 1 || n > (int) ids.size()) return false;
    recall (ids[(size_t) (n - 1)]);
    return true;
}

std::vector<int> Session::knobScope (int k) const
{
    std::vector<int> out;
    const auto& kn = knobs[(size_t) k];
    std::vector<const Snapshot*> ss;
    for (auto id : kn.snaps) if (auto* s = findSnap (id)) ss.push_back (s);
    for (int i = 0; i < kMorphEnd; ++i)
    {
        if (! engine.control (i).morph) continue;
        bool tied = false;
        for (const auto& l : kn.links) tied = tied || l.ctl == i;
        if (tied) { out.push_back (i); continue; }
        if (ss.size() < 2) continue;
        float lo = 2.0f, hi = -2.0f; bool all = true;
        for (auto* s : ss) { const float v = s->values[(size_t) i]; if (v < 0.0f) { all = false; break; } lo = std::min (lo, v); hi = std::max (hi, v); }
        if (all && hi - lo > 1e-4f) out.push_back (i);
    }
    return out;
}

// ---------------------------------------------------------------------------------------- LEARN
// The knob goes to its full turn (what it already pushes, it pushes), and the scene under the
// knobs is remembered. What the hand then does to a knob changes that scene, as it always does.
void Session::learnBegin (int k)
{
    if (k < 0 || k >= 16) return;
    if (learning >= 0) learnKeep();
    auto& kn = knobs[(size_t) k];
    if (kn.depth < 0.1f) kn.depth = 1.0f;           // (a knob with its depth at nothing can be shown nothing)
    learnWas = engine.target (cx::kSnap + k);
    engine.set (cx::kSnap + k, 1.0f);
    applyMorph (glideBeats());
    learnBase = base;
    learning = k;
    touch();
    if (onChange) onChange();
}

bool Session::learnMoved (int ctl) const
{
    return learning >= 0 && reaches (engine, ctl) && ctl < (int) learnBase.size() && std::abs (base[(size_t) ctl] - learnBase[(size_t) ctl]) > 0.002f;
}

int Session::learnMoves() const
{
    int n = 0;
    for (int i = 0; i < kMorphEnd; ++i) if (learnMoved (i)) ++n;
    return n;
}

// Each knob that was moved is tied to the knob by how far it was moved (added to what tied it
// before), and the scene under the knobs is put back as it was: with the knob at full the rig
// sounds exactly as it does, and turned back those knobs go back to where they were.
void Session::learnKeep()
{
    if (learning < 0) return;
    auto& kn = knobs[(size_t) learning];
    for (int i = 0; i < kMorphEnd; ++i)
    {
        if (! learnMoved (i)) continue;
        const float d = (base[(size_t) i] - learnBase[(size_t) i]) / kn.depth;
        auto it = std::find_if (kn.links.begin(), kn.links.end(), [i] (const MacroKnob::Link& l) { return l.ctl == i; });
        if (it == kn.links.end()) kn.links.push_back ({ i, d }); else it->amount += d;
        base[(size_t) i] = learnBase[(size_t) i];
    }
    for (auto& l : kn.links) l.amount = juce::jlimit (-1.0f, 1.0f, l.amount);
    kn.links.erase (std::remove_if (kn.links.begin(), kn.links.end(), [] (const MacroKnob::Link& l) { return std::abs (l.amount) < 0.004f; }), kn.links.end());
    std::sort (kn.links.begin(), kn.links.end(), [] (const MacroKnob::Link& a, const MacroKnob::Link& b) { return a.ctl < b.ctl; });
    learning = -1;
    learnBase.clear();
    applyMorph (glideBeats());
    touch();
    if (onChange) onChange();
}

// The knobs moved since LEARN go back, and the knob to where it stood.
void Session::learnDrop()
{
    if (learning < 0) return;
    for (int i = 0; i < kMorphEnd; ++i) if (learnMoved (i)) base[(size_t) i] = learnBase[(size_t) i];
    engine.set (cx::kSnap + learning, learnWas);
    learning = -1;
    learnBase.clear();
    applyMorph (glideBeats());
    touch();
    if (onChange) onChange();
}

// ------------------------------------------------------------------------------------ snapshots
const Snapshot* Session::findSnap (juce::int64 id) const
{
    for (auto& s : library) if (s.id == id) return &s;
    return nullptr;
}

int Session::indexOf (juce::int64 id) const
{
    for (size_t i = 0; i < library.size(); ++i) if (library[i].id == id) return (int) i;
    return -1;
}

Snapshot Session::capture() const
{
    Snapshot s;
    s.id = juce::Time::currentTimeMillis();
    while (findSnap (s.id) != nullptr) ++s.id;
    s.time = juce::Time::getCurrentTime();
    s.values.assign ((size_t) cx::kCount, -1.0f);
    for (int i = 0; i < cx::kCount; ++i)
    {
        const auto& c = engine.control (i);
        if (c.snapshot && c.used) s.values[(size_t) i] = engine.target (i);
    }
    for (int l = 0; l < kLanes; ++l)
    {
        s.patterns[(size_t) l] = engine.pattern (l);
        for (int k = 0; k < kSlots; ++k)
        {
            s.inserts[(size_t) l][(size_t) k] = engine.insertType (l, k);
            s.insSetup[(size_t) l][(size_t) k] = engine.getSetup ({ 1, l, k });
            s.insFiles[(size_t) l][(size_t) k] = engine.insertFiles (l, k);
        }
        s.gen[(size_t) l] = genType[(size_t) l];
        s.setup[(size_t) l] = engine.getSetup ({ 0, l, 0 });
    }
    for (int r = 0; r < 2; ++r) s.retSetup[(size_t) r] = engine.getSetup ({ 2, 0, r });
    s.busSetup = engine.getSetup ({ 3, 0, 0 });
    for (int l = 0; l < kLanes; ++l) s.laneFiles[(size_t) l] = engine.laneFile (l);
    s.hasSetup = true;
    s.hasBus = true;
    s.hasInsFiles = true;
    s.hasFiles = true;
    s.made = madeNow (s);
    return s;
}

// ------------------------------------------------------------------- the notes from outside
void Session::pullHeard()
{
    Engine::HeardNote buf[256];
    for (;;)
    {
        const int n = engine.takeHeardNotes (buf, 256);
        for (int i = 0; i < n; ++i)
        {
            const auto& h = buf[i];
            if (h.kind >= 2) { for (auto& v : heardLog) v.clear(); heardFrom = h.ppq; continue; }      // the clock set off again, or some were lost: what is known starts here
            if (h.lane >= kLanes) continue;
            auto& v = heardLog[h.lane];
            if (h.kind == 1) v.push_back ({ h.ppq, -1.0, h.note, h.vel });
            else for (auto it = v.rbegin(); it != v.rend(); ++it) if (it->note == h.note && it->len < 0.0) { it->len = std::max (0.0, h.ppq - it->on); break; }
        }
        if (n < 256) break;
    }
    // no further back than seventeen bars: eight to be a loop, eight more to show that it is one
    const double horizon = engine.songPos() - 68.0;
    if (horizon > heardFrom)
    {
        heardFrom = horizon;
        for (auto& v : heardLog) v.erase (std::remove_if (v.begin(), v.end(), [horizon] (const HeardEv& e) { return e.on < horizon; }), v.end());
    }
}

bool Session::heardPattern (int l, Pattern& p, juce::var* raw) const
{
    const auto& log = heardLog[(size_t) l];
    if (log.empty()) return false;
    // A note up to a quarter of a step early is the next step's (a player ahead of the beat, a
    // clock's jitter); anything later in a step is that step's (swing, a ratchet's later hits).
    constexpr double early = 0.25 * kStepBeats;
    auto stepOf = [] (double q) { return (juce::int64) std::floor ((q + early) / kStepBeats); };
    const double now = engine.songPos();
    const juce::int64 kEnd = (juce::int64) std::floor (now / kStepBeats);          // the step under way is not whole yet
    const juce::int64 kFrom = std::max<juce::int64> (0, (juce::int64) std::ceil (heardFrom / kStepBeats - 1.0e-9));
    const int W = (int) juce::jlimit<juce::int64> (0, 2 * kMaxSteps, kEnd - kFrom);
    if (W < 1) return false;
    const bool pitched = lanes()[(size_t) l].pitched;
    struct Cell { bool on = false; int note = 0, vel = 0, ratchet = 1; double first = 0.0, last = 0.0, len = 0.0; };
    std::vector<Cell> win ((size_t) W);
    for (auto& e : log)
    {
        const juce::int64 k = stepOf (e.on);
        if (k < kEnd - W || k >= kEnd) continue;
        auto& c = win[(size_t) (k - (kEnd - W))];
        const double len = e.len >= 0.0 ? e.len : std::max (0.0, now - e.on);
        if (! c.on) { c.on = true; c.note = e.note; c.vel = e.vel; c.first = c.last = e.on; c.len = len; continue; }
        if (e.on - c.last > 0.1 * kStepBeats) { c.ratchet = std::min (4, c.ratchet + 1); c.last = e.on; }      // another hit in the step: a ratchet
        else if (e.on - c.first < 0.1 * kStepBeats && e.note < c.note) { c.note = e.note; c.len = len; }      // a chord: a step holds one note, the lowest
    }
    int lately = 0;
    for (int i = std::max (0, W - kMaxSteps); i < W; ++i) lately += win[(size_t) i].on ? 1 : 0;
    if (lately == 0) return false;
    // How long the part is: the shortest of one, two, four and eight bars that the last bars
    // repeat. If none does (or too little is known to tell), the last bars as they were, up to eight.
    auto same = [&] (int L)
    {
        for (int i = 0; i < L; ++i)
        {
            const auto& a = win[(size_t) (W - L + i)]; const auto& b = win[(size_t) (W - 2 * L + i)];
            if (a.on != b.on || (a.on && ((pitched && a.note != b.note) || a.ratchet != b.ratchet))) return false;
        }
        return true;
    };
    int L = 0;
    for (int c : { 16, 32, 64, 128 }) if (L == 0 && W >= 2 * c && same (c)) L = c;
    for (int c : { 128, 64, 32, 16 }) if (L == 0 && W >= c) L = c;
    if (L == 0) L = 16;
    p = Pattern();
    p.length = L;
    const int oct = lanes()[(size_t) l].octave;
    for (auto& st : p.steps) st.octave = oct;
    for (int i = std::max (0, W - L); i < W; ++i)
    {
        const auto& c = win[(size_t) i];
        if (! c.on) continue;
        const juce::int64 k = kEnd - W + i;
        auto& st = p.steps[(size_t) (((k % L) + L) % L)];
        st.on = true;
        st.vel = juce::jlimit (1.0f / 127.0f, 1.0f, (float) c.vel / 127.0f);
        st.ratchet = c.ratchet;
        const double steps = c.len / kStepBeats;
        st.gate = (float) (c.ratchet > 1 ? juce::jlimit (0.05, 1.0, steps * c.ratchet) : juce::jlimit (0.05, (double) kMaxSteps, std::round (steps * 100.0) / 100.0));
        if (pitched) { st.note = c.note % 12; st.octave = juce::jlimit (0, 8, c.note / 12 - 2); }
    }
    bool any = false;
    for (int i = 0; i < L; ++i) any = any || p.steps[(size_t) i].on;
    if (! any) return false;                    // (it had been played, and had stopped: nothing was coming in)
    if (raw != nullptr)
    {
        juce::Array<var> notes;
        const double loop = (double) L * kStepBeats;
        for (auto& e : log)
        {
            const juce::int64 k = stepOf (e.on);
            if (k < kEnd - L || k >= kEnd) continue;
            double at = std::fmod (e.on, loop);
            if (at < 0.0) at += loop;
            juce::Array<var> n;
            n.add (std::round (at * 10000.0) / 10000.0); n.add (e.len >= 0.0 ? std::round (e.len * 10000.0) / 10000.0 : -1.0); n.add (e.note); n.add (e.vel);
            notes.add (var (n));
        }
        *raw = var (notes);
    }
    return true;
}

Pattern Session::played (const Snapshot& s, int l)
{
    const Pattern& own = s.patterns[(size_t) l];
    if (! s.hasPlayed || ! s.heardOn[(size_t) l]) return own;
    const Pattern& in = s.heard[(size_t) l];
    const int la = juce::jlimit (1, kMaxSteps, own.length), lb = juce::jlimit (1, kMaxSteps, in.length);
    bool any = false;
    for (int i = 0; i < la; ++i) any = any || own.steps[(size_t) i].on;
    if (s.seqMuted[(size_t) l] || ! any) return in;
    // both were sounding: the lane's own steps, and where a note came from outside, that note
    Pattern m;
    int len = std::lcm (la, lb);
    if (len > kMaxSteps) len = std::max (la, lb);
    m.length = len;
    for (int i = 0; i < len; ++i) m.steps[(size_t) i] = in.steps[(size_t) (i % lb)].on ? in.steps[(size_t) (i % lb)] : own.steps[(size_t) (i % la)];
    for (int i = len; i < kMaxSteps; ++i) m.steps[(size_t) i].octave = lanes()[(size_t) l].octave;
    return m;
}

// What a snapshot was made with: this build, and the print of every definition its values lean on.
// A later build, or another tool, reads the values by these (the definitions themselves are kept in
// the session file).
juce::var Session::madeNow (const Snapshot& s) const
{
    const auto& d = engine.defs();
    auto* o = new juce::DynamicObject();
    o->setProperty ("app", juce::String (JUCE_APPLICATION_VERSION_STRING) + " " + SF000_COMMIT);
    juce::Array<var> slots;
    auto* ln = new juce::DynamicObject();
    for (int l = 0; l < kLanes; ++l) { slots.add (d.slots[l]); ln->setProperty (d.slots[l], d.lane[(size_t) l]); }
    o->setProperty ("slots", slots);            // the order every per-lane list in the snapshot is in
    o->setProperty ("lanes", var (ln));
    auto* fx = new juce::DynamicObject();
    for (int l = 0; l < kLanes; ++l)
        for (int k = 0; k < kSlots; ++k)
            if (const int t = s.inserts[(size_t) l][(size_t) k]; t >= 0 && t < (int) d.fx.size()) fx->setProperty (d.fx[(size_t) t].first, d.fx[(size_t) t].second);
    o->setProperty ("fx", var (fx));
    auto* rt = new juce::DynamicObject();
    rt->setProperty ("dub", d.dub); rt->setProperty ("space", d.space); rt->setProperty ("conv", d.conv);
    o->setProperty ("returns", var (rt));
    if (s.hasBus) o->setProperty ("bus", d.bus);
    if ((int) s.values.size() > cx::kPkG && s.values[(size_t) cx::kPkG] >= 0.0f) o->setProperty ("pockets", d.pockets);
    o->setProperty ("chassis", d.chassis);
    return var (o);
}

void Session::findLibraryId (const juce::String& fromFile)
{
    // Sixteen hex digits, made once. Kept in a file of its own beside the session as well as in it,
    // so a build from before this was kept, which drops it from the session, does not lose it.
    auto good = [] (const juce::String& t) { return t.length() == 16 && t.containsOnly ("0123456789abcdef"); };
    const auto f = stateFile().getSiblingFile ("library-id.txt");
    const auto onDisk = useDisk && f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();
    libraryId = good (onDisk) ? onDisk : good (fromFile) ? fromFile : juce::Uuid().toString().substring (0, 16);
    if (useDisk && onDisk != libraryId) { f.getParentDirectory().createDirectory(); f.replaceWithText (libraryId); dirty = true; }
}

void Session::sign()
{
    for (auto& s : library)
        if (s.uid.isEmpty())
        {
            s.uid = libraryId + "-" + juce::String::toHexString (s.id);
            if (auto* o = s.kept.getDynamicObject()) o->setProperty ("uid", s.uid);
            dirty = true;
        }
}

juce::File Session::clipFile (juce::int64 id) const { return stateFile().getSiblingFile ("clips").getChildFile (juce::String (id) + ".ogg"); }

void Session::waitForClips() { while (clipWriter.getNumJobs() > 0) juce::Thread::sleep (2); }

// Written by a thread of its own: Ogg Vorbis, a hundred-odd kilobytes for four bars.
void Session::keepClip (juce::int64 id, const Engine::Heard& heard)
{
    auto audio = std::make_shared<juce::AudioBuffer<float>> (heard.audio);
    const auto file = clipFile (id);
    const double rate = heard.sampleRate;
    clipWriter.addJob ([audio, file, rate]
    {
        file.getParentDirectory().createDirectory();
        const auto part = file.getSiblingFile (file.getFileName() + ".part");
        part.deleteFile();
        bool ok = false;
        if (auto out = part.createOutputStream())
        {
            juce::OggVorbisAudioFormat ogg;
            std::unique_ptr<juce::AudioFormatWriter> w (ogg.createWriterFor (out.get(), rate, 2, 16, {}, 6));
            if (w != nullptr) { out.release(); ok = w->writeFromAudioSampleBuffer (*audio, 0, audio->getNumSamples()); }
        }
        if (ok) part.moveFileTo (file); else part.deleteFile();
    });
}

std::vector<juce::int64> Session::mapCell (int row, int col) const
{
    std::vector<const Snapshot*> in;
    for (auto& s : library) if (s.profile.has && energyRow (s.profile) == row && brightCol (s.profile) == col) in.push_back (&s);
    std::stable_sort (in.begin(), in.end(), [] (const Snapshot* a, const Snapshot* b) { return a->profile.energy < b->profile.energy; });
    std::vector<juce::int64> ids;
    for (auto* s : in) ids.push_back (s->id);
    return ids;
}

std::vector<juce::int64> Session::recent (int most) const
{
    std::vector<const Snapshot*> in;
    for (auto& s : library) if (s.profile.has) in.push_back (&s);
    std::stable_sort (in.begin(), in.end(), [] (const Snapshot* a, const Snapshot* b) { return a->time > b->time; });
    std::vector<juce::int64> ids;
    for (size_t i = 0; i < in.size() && (int) i < most; ++i) ids.push_back (in[i]->id);
    return ids;
}

std::vector<juce::int64> Session::fitting (const Profile& now, Fit kind, int most) const
{
    std::vector<juce::int64> ids;
    if (! now.has) return ids;
    struct Cand { const Snapshot* s; float colour, step; };
    std::vector<Cand> c;
    for (auto& s : library)
    {
        if (! s.profile.has) continue;
        const float step = s.profile.energy - now.energy;
        if (kind == Fit::more && step < 0.04f) continue;
        if (kind == Fit::less && step > -0.04f) continue;
        c.push_back ({ &s, distance (s.profile, now, kind != Fit::like), std::abs (step) });
    }
    if (kind == Fit::like) std::stable_sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.colour < b.colour; });
    else
    {
        // the ones nearest in colour, then in order of the step
        std::stable_sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.colour < b.colour; });
        if ((int) c.size() > most) c.resize ((size_t) most);
        std::stable_sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.step < b.step; });
    }
    for (size_t i = 0; i < c.size() && (int) i < most; ++i) ids.push_back (c[i].s->id);
    return ids;
}

const Profile& Session::heardNow()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (nowAt == 0 || now - nowAt > 1500)
    {
        Engine::Heard h;
        nowProfile = engine.lastHeard (h) ? profileOf (h) : Profile();
        nowAt = now == 0 ? 1u : now;
    }
    return nowProfile;
}

// A clip the engine may still be fading out is kept a second past being replaced.
void Session::pruneCues()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (size_t i = cueKept.size(); i-- > 0;)
        if (cueKept[i].second != 0 && now - cueKept[i].second > 1000) cueKept.erase (cueKept.begin() + (long) i);
}

bool Session::cue (juce::int64 id)
{
    const auto now = juce::Time::getMillisecondCounter();
    auto retire = [this, now] { for (auto& k : cueKept) if (k.second == 0) k.second = now == 0 ? 1u : now; };
    pruneCues();
    const Snapshot* s = id != 0 ? findSnap (id) : nullptr;
    std::unique_ptr<Engine::CueClip> clip;
    if (s != nullptr)
    {
        if (! clipFile (id).existsAsFile()) waitForClips();              // (taken a moment ago: still being written)
        if (auto in = clipFile (id).createInputStream())
        {
            juce::OggVorbisAudioFormat ogg;
            if (std::unique_ptr<juce::AudioFormatReader> r { ogg.createReaderFor (in.release(), true) })
                if (r->lengthInSamples > 1 && r->lengthInSamples < 48000 * 60)
                {
                    clip = std::make_unique<Engine::CueClip>();
                    clip->audio.setSize (2, (int) r->lengthInSamples);
                    r->read (&clip->audio, 0, (int) r->lengthInSamples, 0, true, true);
                    clip->bars = s->profile.has ? s->profile.bars : 0;
                    clip->rate = r->sampleRate;
                }
        }
    }
    retire();
    const auto was = cueId;
    if (clip == nullptr) { engine.setCue (nullptr); cueId = 0; }
    else { engine.setCue (clip.get()); cueKept.push_back ({ std::move (clip), 0u }); cueId = id; }
    if (cueId != was && onChange) onChange();                       // (the library shows which is in the headphones)
    return cueId != 0;
}

juce::int64 Session::take()
{
    auto s = capture();
    // what each lane was playing that its steps do not say: the notes from outside, and whether its own sequencer was heard
    pullHeard();
    for (int l = 0; l < kLanes; ++l)
    {
        s.heardOn[(size_t) l] = heardPattern (l, s.heard[(size_t) l], &s.heardNotes[(size_t) l]);
        s.seqMuted[(size_t) l] = engine.target (cx::lane (l, cx::kSeqMute)) > 0.5f;
    }
    s.hasPlayed = true;
    int n = (int) library.size() + 1;
    auto nameFor = [] (int k) { return "SNAP " + juce::String (k).paddedLeft ('0', 3); };
    auto taken = [this] (const juce::String& nm) { for (auto& x : library) if (x.name == nm) return true; return false; };
    while (taken (nameFor (n))) ++n;
    s.name = nameFor (n);
    lastTakenAt = s.time.formatted ("%H:%M:%S");
    // Its sound, with nothing asked: the bars the rig has held this state through, profiled and kept
    // as a clip. Held for four bars already: those. Something has just moved, or is still on its
    // way: it waits for them (finishTakes), so the clip is the snapshot and not the road to it.
    const auto id = s.id;
    const auto stamp = engine.stateStamp();
    Engine::Heard heard;
    const bool heldNow = engine.heldHeard (heard, stamp) && heard.bars >= 4;
    library.push_back (s);
    sign();
    if (heldNow) settle (id, heard, true);
    else if (engine.isRunning())
    {
        Waiting w;
        w.id = id; w.stamp = stamp;
        w.hasBefore = engine.lastHeard (w.before);
        waiting.push_back (std::move (w));
        startTimer (400);
    }
    else if (engine.lastHeard (heard)) settle (id, heard, false);     // the clock is stopped: what was last heard, if anything
    touch();
    if (onLibrary) onLibrary();
    return id;
}

void Session::settle (juce::int64 id, const Engine::Heard& heard, bool steady)
{
    const int i = indexOf (id);
    if (i < 0) return;
    auto& s = library[(size_t) i];
    s.profile = profileOf (heard);
    s.profile.steady = steady;
    if (useDisk) keepClip (id, heard);
    touch();
}

void Session::finishTakes (bool all)
{
    if (waiting.empty()) return;
    bool any = false;
    for (size_t k = 0; k < waiting.size();)
    {
        auto& w = waiting[k];
        Engine::Heard h;
        const bool some = indexOf (w.id) >= 0 && engine.heldHeard (h, w.stamp);
        const bool moved = engine.stateStamp() != w.stamp || ! engine.isRunning();
        if (indexOf (w.id) < 0) {}                                              // deleted meanwhile
        else if (some && h.bars >= h.most) { settle (w.id, h, true); any = true; }       // four bars (fewer only if the tape cannot hold four: an outside clock slower than 60)
        else if (! (moved || all)) { ++k; continue; }                           // not yet: it is still holding
        else if (some) { settle (w.id, h, true); any = true; }                  // it moved on: the bars that were held, fewer than four
        else if (w.hasBefore) { settle (w.id, w.before, false); any = true; }   // none were: the bars it was taken over
        waiting.erase (waiting.begin() + (long) k);
    }
    if (any)
    {
        if (onChange) onChange();
        if (onLibrary) onLibrary();
    }
}

// Whether two CONFIG states differ in anything they both name.
static bool setupDiffers (const Engine::SetupState& a, const Engine::SetupState& b)
{
    for (auto& x : a)
        for (auto& y : b)
            if (x.first == y.first && std::abs (x.second - y.second) > 1.0e-4f) return true;
    return false;
}

// A lane's setup as a snapshot holds it, whole: one taken before MOTION could be set says nothing
// of it, and means the two of them off (as they were then).
static Engine::SetupState withMotion (const Engine& engine, Engine::Target t, Engine::SetupState st)
{
    for (auto& kv : st) if (Engine::isMotionId (kv.first)) return st;
    for (auto& kv : engine.motionOff (t)) st.push_back (kv);
    return st;
}

// A lane's instrument is to change in a way that cannot travel: something CONFIG sets (its model,
// its layer, a sample's ends), or one of its knobs that is a menu or a switch.
bool Session::laneJumps (const Snapshot& s, int l) const
{
    if (s.hasFiles && s.laneFiles[(size_t) l].isNotEmpty() && s.laneFiles[(size_t) l] != engine.laneFile (l)) return true;     // (another sample: it cannot travel)
    if (s.hasSetup && setupDiffers (engine.getSetup ({ 0, l, 0 }), withMotion (engine, { 0, l, 0 }, s.setup[(size_t) l]))) return true;
    for (int k = 0; k < kLaneKnobs; ++k)
    {
        const int i = cx::lane (l, k);
        const auto& c = engine.control (i);
        const float v = i < (int) s.values.size() ? s.values[(size_t) i] : -1.0f;
        if (v >= 0.0f && c.used && ! c.glides && std::abs (v - engine.target (i)) > 1.0e-4f) return true;
    }
    return false;
}

// An insert stays as it is through a recall: the same effect holding the same files, with the same
// CONFIG, and no menu or switch of its own to move. (Its knobs may differ: they travel.)
bool Session::insertStays (const Snapshot& s, int l, int k) const
{
    const int type = s.inserts[(size_t) l][(size_t) k];
    const auto& refs = s.insFiles[(size_t) l][(size_t) k];
    if (! (s.hasInsFiles ? engine.insertHolds (l, k, type, refs) : engine.insertType (l, k) == type)) return false;
    if (type < 0) return true;
    if (s.hasSetup && setupDiffers (engine.getSetup ({ 1, l, k }), withMotion (engine, { 1, l, k }, s.insSetup[(size_t) l][(size_t) k]))) return false;
    for (int c = 0; c < kCells; ++c)
    {
        const int i = cx::ins (l, k, c);
        const auto& ct = engine.control (i);
        const float v = i < (int) s.values.size() ? s.values[(size_t) i] : -1.0f;
        if (v >= 0.0f && ct.used && ! ct.glides && ct.kind != Kind::Hold && std::abs (v - engine.target (i)) > 1.0e-4f) return false;
    }
    return true;
}

void Session::applySnapshot (const Snapshot& s, double glide, const std::array<bool, kLanes>* notes)
{
    // What can travel, travels: sound knobs glide to their places. What cannot (an effect put in or
    // taken out, a model, a menu, a switch) is crossed over instead, over the same time: the one
    // that was there carries on as it is and fades out while the new one arrives beside it.
    // 1. the lanes whose instrument is to change that way: the one playing leaves as it stands, and
    //    a copy of it takes its place to be changed by what follows
    //    (with the audio not yet started there is nothing playing to leave: the change is simply made)
    if (engine.isPrepared()) for (int l = 0; l < kLanes; ++l) if (laneJumps (s, l)) engine.forkVoice (l, glide);
    // 2. the inserts: an effect that changes comes whole (the effect, its CONFIG, the files it
    //    held, its knobs); one that stays is left running, and its knobs travel
    std::array<std::array<bool, kSlots>, kLanes> stays {};
    for (int l = 0; l < kLanes; ++l)
        for (int k = 0; k < kSlots; ++k)
        {
            stays[(size_t) l][(size_t) k] = insertStays (s, l, k);
            if (stays[(size_t) l][(size_t) k]) continue;
            const int type = s.inserts[(size_t) l][(size_t) k];
            const auto& refs = s.insFiles[(size_t) l][(size_t) k];
            float cells[kCells];
            for (int c = 0; c < kCells; ++c) { const int i = cx::ins (l, k, c); cells[c] = i < (int) s.values.size() ? s.values[(size_t) i] : -1.0f; }
            if (s.hasInsFiles && type >= 0) engine.expectInsertFiles (l, k, refs);
            engine.setInsertType (l, k, type, false, s.hasSetup ? &s.insSetup[(size_t) l][(size_t) k] : nullptr, glide, cells);
        }
    // 3. every stored value. Sound knobs wait for the morph below; the knobs of an effect that
    //    stays travel; the rest go at once (they reach only what has just arrived)
    for (int i = 0; i < cx::kCount && i < (int) s.values.size(); ++i)
    {
        const float v = s.values[(size_t) i];
        if (v < 0.0f) continue;
        const bool morph = engine.control (i).morph && i < kMorphEnd;
        base[(size_t) i] = v;
        if (morph) continue;
        const int k = i < cx::kRet ? i % cx::kPerLane : -1;
        const bool cell = k >= cx::kIns && k < cx::kGen;
        const bool travels = cell && engine.control (i).glides && stays[(size_t) (i / cx::kPerLane)][(size_t) ((k - cx::kIns) / kCells)];
        // (the key every lane follows goes over on the beat the knobs set off on: with the sound, not ahead of it)
        engine.set (i, v, travels || i == cx::kKey ? glide : 0.0);
    }
    for (int l = 0; l < kLanes; ++l)
    {
        if (notes == nullptr || (*notes)[(size_t) l])
        {
            // What the lane was playing: into its sequencer, on the next bar line while the clock
            // runs. A snapshot that says what was sounding sets the sequencer's mute to match.
            engine.setPattern (l, played (s, l), true);
            genType[(size_t) l] = s.gen[(size_t) l];
            if (s.hasPlayed) engine.set (cx::lane (l, cx::kSeqMute), ! s.heardOn[(size_t) l] && s.seqMuted[(size_t) l] ? 1.0f : 0.0f, glide);      // (on that beat too)
        }
        if (s.hasSetup)
        {
            engine.applySetup ({ 0, l, 0 }, withMotion (engine, { 0, l, 0 }, s.setup[(size_t) l]));
            for (int k = 0; k < kSlots; ++k) engine.applySetup ({ 1, l, k }, withMotion (engine, { 1, l, k }, s.insSetup[(size_t) l][(size_t) k]));
        }
        // the sample it held, if it holds another now (the voice that had the old one is on its way out: see laneJumps)
        if (s.hasFiles && s.laneFiles[(size_t) l].isNotEmpty() && s.laneFiles[(size_t) l] != engine.laneFile (l))
            engine.loadFile (l, juce::File (s.laneFiles[(size_t) l]));
    }
    if (s.hasSetup) for (int r = 0; r < 2; ++r) engine.applySetup ({ 2, 0, r }, withMotion (engine, { 2, 0, r }, s.retSetup[(size_t) r]));
    if (s.hasBus) engine.applySetup ({ 3, 0, 0 }, s.busSetup);
    if (learning >= 0) learnBase = base;            // (a recall while a knob is being taught: what it is shown is measured from the recalled scene)
    applyMorph (glide);
}
void Session::recall (juce::int64 id)
{
    Scope all;
    all.notes = recallNotes;
    recall (id, all);
}

void Session::recallWhole (juce::int64 id)
{
    if (auto* s = findSnap (id))
    {
        const Snapshot copy = *s;
        applySnapshot (copy, recallGlide());
        baseId = id;
        selStep = -1;
        touch();
        if (onChange) onChange();
    }
}

void Session::recall (juce::int64 id, const Scope& scope)
{
    if (scope.all()) { recallWhole (id); return; }
    auto* s = findSnap (id);
    if (s == nullptr) return;
    // The snapshot, with every part that is not chosen put back as it stands now: applied whole,
    // those parts find nothing to change. (A sound knob's place here is the scene under the
    // snapshot knobs, not what they have made of it: that is what a recall sets.)
    Snapshot m = *s;
    const Snapshot now = capture();
    if (m.values.size() < (size_t) cx::kCount) m.values.resize ((size_t) cx::kCount, -1.0f);
    auto keep = [&] (int from, int to)
    {
        for (int i = from; i < to; ++i)
            m.values[(size_t) i] = now.values[(size_t) i] < 0.0f ? -1.0f : (engine.control (i).morph && i < kMorphEnd ? base[(size_t) i] : now.values[(size_t) i]);
    };
    std::array<bool, kLanes> notes;
    for (int l = 0; l < kLanes; ++l)
    {
        // without its notes a lane's sequencer stays as it is: its steps, its generator and that generator's knobs
        notes[(size_t) l] = scope.lane[(size_t) l] && scope.notes;
        if (! notes[(size_t) l]) keep (cx::gen (l, 0), cx::gen (l, 0) + 4);
        if (scope.lane[(size_t) l]) continue;
        keep (l * cx::kPerLane, (l + 1) * cx::kPerLane);
        m.inserts[(size_t) l] = now.inserts[(size_t) l];
        if (m.hasSetup) { m.setup[(size_t) l] = now.setup[(size_t) l]; m.insSetup[(size_t) l] = now.insSetup[(size_t) l]; }
        if (m.hasInsFiles) m.insFiles[(size_t) l] = now.insFiles[(size_t) l];
        m.laneFiles[(size_t) l] = {};
    }
    if (! scope.returns) { keep (cx::kRet, cx::kBus); if (m.hasSetup) m.retSetup = now.retSetup; }
    if (! scope.bus) { keep (cx::kBus, cx::kRetMS); m.hasBus = false; }
    if (! scope.master) keep (cx::kMaster, cx::kSnap);
    applySnapshot (m, recallGlide(), &notes);
    baseId = id;
    selStep = -1;
    touch();
    if (onChange) onChange();
}

void Session::remove (juce::int64 id)
{
    const int i = indexOf (id);
    if (i < 0) return;
    library.erase (library.begin() + i);
    waiting.erase (std::remove_if (waiting.begin(), waiting.end(), [id] (const Waiting& w) { return w.id == id; }), waiting.end());
    if (cueId == id) cue (0);
    if (useDisk) { waitForClips(); clipFile (id).deleteFile(); }
    for (auto& k : knobs) k.snaps.erase (std::remove (k.snaps.begin(), k.snaps.end(), id), k.snaps.end());
    arrangement.erase (std::remove_if (arrangement.begin(), arrangement.end(), [id] (const ArrPart& p) { return p.snap == id; }), arrangement.end());      // (the parts it was, in the arrangement)
    if (baseId == id) baseId = 0;
    applyMorph (0.0);
    sweepSamples();                     // the samples only it held go with it
    touch();
}

void Session::rename (juce::int64 id, const juce::String& name)
{
    const int i = indexOf (id);
    if (i < 0 || name.trim().isEmpty()) return;
    auto& s = library[(size_t) i];
    s.name = name.trim().toUpperCase().substring (0, 24);
    if (auto* o = s.kept.getDynamicObject()) o->setProperty ("name", s.name);
    touch();
    if (onLibrary) onLibrary();
}

// --------------------------------------------------------------------------- patterns, inserts
void Session::setPattern (int l, const Pattern& p)
{
    engine.setPattern (l, p);
    touch();
}

void Session::regenerate (int l)
{
    Pattern p = engine.pattern (l);
    GenParams g;
    g.type = genType[(size_t) l];
    g.hits = engine.target (cx::gen (l, 0)); g.rotate = engine.target (cx::gen (l, 1));
    g.accents = engine.target (cx::gen (l, 2)); g.seed = engine.target (cx::gen (l, 3));
    generate (p, g);
    engine.setPattern (l, p);
}

// ---- the chosen step, as knobs
static const char* const kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
// a knob of n shares: which share a number is in, and the number in the middle of a share
static int shareOf (int v7, int n)  { return juce::jlimit (0, n - 1, juce::jlimit (0, 127, v7) * n / 128); }
static int shareMid (int i, int n)  { return juce::jlimit (0, 127, (i * 128 + 64) / n); }
// the gate's law: to the step's own edge at the knob's middle, then on to the most it may be
static float gateMost (const Pattern& p, int i) { return p.steps[(size_t) i].ratchet == 1 ? (float) stepsToNext (p, i) + 0.25f : 1.0f; }

int Session::stepKnob (int which, int has) const
{
    const Pattern p = pattern (activeLane);
    const int i = selStep;
    if (i < 0 || i >= p.length) return -1;
    const auto& s = p.steps[(size_t) i];
    auto shared = [has] (int at, int n) { return has >= 0 && shareOf (has, n) == at ? has : shareMid (at, n); };
    switch (which)
    {
        case stPos:     return shared (i, p.length);
        case stVel:     return juce::jlimit (0, 127, (int) std::lround ((s.vel - 0.15f) / 0.85f * 127.0f));
        case stGate:
        {
            const float most = gateMost (p, i);
            if (s.gate <= 1.0f || most <= 1.0f) return juce::jlimit (0, 64, (int) std::lround ((s.gate - 0.15f) / 0.85f * 64.0f));
            return juce::jlimit (64, 127, 64 + (int) std::lround ((s.gate - 1.0f) / (most - 1.0f) * 63.0f));
        }
        case stRatchet: return shared (juce::jlimit (1, 4, s.ratchet) - 1, 4);
        case stNote:    return lanes()[(size_t) activeLane].pitched ? shared (juce::jlimit (0, 11, s.note), 12) : -1;
        case stOctave:  return lanes()[(size_t) activeLane].pitched ? shared (juce::jlimit (1, 5, s.octave) - 1, 5) : -1;
        default:        return -1;
    }
}

void Session::setStepKnob (int which, int v7)
{
    const int l = activeLane;
    Pattern p = pattern (l);
    v7 = juce::jlimit (0, 127, v7);
    if (which == stPos)
    {
        // through the pattern's steps; the screen's page of sixteen goes with it
        const int i = shareOf (v7, std::max (1, p.length));
        if (i == selStep) return;
        selStep = i; page = i / 16;
        if (onChange) onChange();
        return;
    }
    const int i = selStep;
    if (i < 0 || i >= p.length) return;
    auto& s = p.steps[(size_t) i];
    const bool pitched = lanes()[(size_t) l].pitched;
    auto light = [&] { if (! s.on) { clearStep (s, lanes()[(size_t) l].octave); s.on = true; } };
    switch (which)
    {
        case stVel:     light(); s.vel = 0.15f + 0.85f * (float) v7 / 127.0f; break;
        case stGate:
        {
            light();
            const float most = gateMost (p, i), dist = (float) stepsToNext (p, i);
            float g = v7 <= 64 ? 0.15f + 0.85f * (float) v7 / 64.0f : 1.0f + (most - 1.0f) * (float) (v7 - 64) / 63.0f;
            if (std::abs (g - dist) < 0.07f) g = dist;         // (it stops at the next lit step's edge; a little past that it is legato into it)
            s.gate = juce::jlimit (0.15f, std::max (1.0f, most), g);
            break;
        }
        case stRatchet: if (! s.on) return; s.ratchet = 1 + shareOf (v7, 4); if (s.ratchet > 1) s.gate = std::min (s.gate, 1.0f); break;
        case stNote:    if (! pitched) return; s.note = shareOf (v7, 12); break;
        case stOctave:  if (! pitched) return; s.octave = 1 + shareOf (v7, 5); break;
        default:        return;
    }
    setPattern (l, p);
    if (onChange) onChange();
}

void Session::stepPress()
{
    const int l = activeLane;
    Pattern p = pattern (l);
    if (selStep < 0 || selStep >= p.length) { selStep = 0; page = 0; }       // (none chosen: the first)
    auto& s = p.steps[(size_t) selStep];
    const int oct = lanes()[(size_t) l].octave;
    if (! s.on) { clearStep (s, oct); s.on = true; } else clearStep (s, oct);
    setPattern (l, p);
    if (onChange) onChange();
}

juce::String Session::stepSays() const
{
    const Pattern p = pattern (activeLane);
    if (selStep < 0 || selStep >= p.length) return {};
    const auto& s = p.steps[(size_t) selStep];
    juce::String t (selStep + 1);
    if (! s.on) return t + " OUT";
    if (lanes()[(size_t) activeLane].pitched) t << " " << kNoteNames[juce::jlimit (0, 11, s.note)] << s.octave;
    return t;
}

void Session::pressGenerator (int l, int type)
{
    // A press writes the pattern at once and arms the knobs; a press on the lit key lets go of them.
    if (genType[(size_t) l] == type) genType[(size_t) l] = -1;
    else { genType[(size_t) l] = type; regenerate (l); }
    touch();
}

void Session::nudgeLane (int l, int dir)
{
    Pattern p = engine.pattern (l);
    nudge (p, dir);
    engine.setPattern (l, p);
    if (l == activeLane && selStep >= 0 && selStep < p.length) selStep = (((selStep + dir) % p.length) + p.length) % p.length;
    touch();
}

void Session::stepInsert (int l, int slot)
{
    int t = engine.insertType (l, slot) + 1;
    if (t >= (int) effects().size()) t = -1;
    engine.setInsertType (l, slot, t, true);
    for (int c = 0; c < kCells; ++c) base[(size_t) cx::ins (l, slot, c)] = engine.target (cx::ins (l, slot, c));
    touch();
}

// ----------------------------------------------------------------------------------------- disk
static Session::Places gPlaces;
static bool gPlacesRead = false, gSettled = false;
static juce::File gLibraryDir;

juce::File Session::homeDir()
{
    if (stateFileForTests != juce::File()) return stateFileForTests.getParentDirectory();
    if (homeForTests != juce::File()) return homeForTests;
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("SF-000");
}

static void readPlaces()
{
    gPlaces = {}; gPlacesRead = true;
    if (Session::stateFileForTests != juce::File()) return;
    const var v = juce::JSON::parse (Session::homeFile ("places.json"));
    if (! v.isObject()) return;
    gPlaces.library = v.getProperty ("library", var()).toString();
    gPlaces.next = v.getProperty ("libraryNext", var()).toString();
    gPlaces.recordings = v.getProperty ("recordings", var()).toString();
}

static void writePlaces()
{
    if (Session::stateFileForTests != juce::File()) return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("library", gPlaces.library);
    o->setProperty ("libraryNext", gPlaces.next);
    o->setProperty ("recordings", gPlaces.recordings);
    Session::homeDir().createDirectory();
    Session::homeFile ("places.json").replaceWithText (juce::JSON::toString (var (o)));
}

const Session::Places& Session::places()
{
    if (! gSettled && stateFileForTests == juce::File()) settlePlaces();
    if (! gPlacesRead) readPlaces();
    return gPlaces;
}

// How a folder is said: from the home folder down.
static juce::String said (const juce::File& f)
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return (f.isAChildOf (home) ? "~/" + f.getRelativePathFrom (home) : f.getFullPathName()).toUpperCase();
}

// A library, from one folder to another: the session, its id, what has been sent online, the clips
// and the samples. (Not the backups: they are of the place they were made in.)
static bool copyLibrary (const juce::File& from, const juce::File& to)
{
    if (! to.createDirectory()) return false;
    bool ok = true;
    for (auto* name : { "session.json", "library-id.txt", "online-sent.json" })
        if (const auto f = from.getChildFile (name); f.existsAsFile()) ok = f.copyFileTo (to.getChildFile (name)) && ok;
    for (auto* name : { "clips", "samples" })
        if (const auto d = from.getChildFile (name); d.isDirectory()) ok = d.copyDirectoryTo (to.getChildFile (name)) && ok;
    return ok;
}

void Session::settlePlaces (bool again)
{
    if (gSettled && ! again) return;
    readPlaces();
    gSettled = true;
    auto& p = gPlaces;
    const juce::File home = homeDir();
    auto dirOf = [&] (const juce::String& t) { return t.isEmpty() || t == "(home)" ? home : juce::File (t); };
    juce::File cur = dirOf (p.library);
    if (p.library.isNotEmpty() && ! cur.isDirectory())
    {
        // its drive is not plugged in, or it has been moved: it stays chosen, and this time the one on this machine is used
        p.libraryMissing = true;
        p.note = "THE LIBRARY FOLDER IS NOT THERE: " + p.library + "\n\nThis time SF-000 is using the library on this computer (" + said (home)
               + "), which may be older or empty. What you do now is kept there, not in the folder you chose. Plug the drive in and open SF-000 again to use the chosen one; or choose another in SETUP.";
        cur = home;
    }
    if (p.next.isNotEmpty())
    {
        const juce::File to = dirOf (p.next);
        if (to == cur) { p.next = {}; writePlaces(); }
        else if (! to.isDirectory() && ! (to.getParentDirectory().isDirectory() && to.createDirectory()))
            p.note = "THE FOLDER CHOSEN FOR THE LIBRARY IS NOT THERE: " + p.next + "\n\nSF-000 is still using " + said (cur) + ". The chosen folder is taken up at the first start that finds it.";
        else if (p.libraryMissing && ! to.getChildFile ("session.json").existsAsFile())
            p.note += "\n\nThe new folder chosen (" + p.next + ") has no library in it, and the one to copy there is not to be found: nothing was moved.";
        else
        {
            const bool there = to.getChildFile ("session.json").existsAsFile();
            const bool ok = there || copyLibrary (cur, to);
            if (ok)
            {
                p.note = "THE LIBRARY IS NOW IN " + said (to) + "\n\n"
                       + (there ? juce::String ("The library that was already in that folder is the one in use.")
                                : "Everything was copied there from " + said (cur) + " (session, clips and samples). The old folder is as it was: delete it when you are sure.");
                p.library = to == home ? juce::String() : to.getFullPathName();
                p.libraryMissing = false;
                cur = to;
            }
            else p.note = "THE LIBRARY COULD NOT BE COPIED TO " + said (to) + "\n\nSF-000 is still using " + said (cur) + ". Check the disk has room and can be written to, and choose it again in SETUP.";
            p.next = {};
            writePlaces();
        }
    }
    gLibraryDir = cur;
}

juce::File Session::libraryDir()
{
    if (stateFileForTests != juce::File()) return stateFileForTests.getParentDirectory();
    if (! gSettled) settlePlaces();
    return gLibraryDir;
}

juce::File Session::usualRecordingsDir()    { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SF-000"); }
juce::File Session::recordingsDir()
{
    const auto& p = places();
    return p.recordings.isNotEmpty() ? juce::File (p.recordings) : usualRecordingsDir();
}

juce::File Session::libraryFolderFor (const juce::File& chosen)
{
    // Not into the top of a disk or a folder of other things: a folder of its own, unless the
    // one chosen is a library already, is empty, or is named for it.
    if (chosen.getChildFile ("session.json").existsAsFile()) return chosen;
    const auto inside = chosen.getChildFile ("SF-000");
    if (inside.getChildFile ("session.json").existsAsFile()) return inside;
    if (chosen.getFileName().equalsIgnoreCase ("SF-000") || chosen.getNumberOfChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles) == 0) return chosen;
    return inside;
}

juce::String Session::chooseLibrary (const juce::File& chosen)
{
    if (stateFileForTests != juce::File()) return {};
    if (! chosen.isDirectory()) return "THAT IS NOT A FOLDER";
    const juce::File to = libraryFolderFor (chosen), cur = libraryDir();
    if (! chosen.hasWriteAccess()) return "SF-000 CANNOT WRITE IN " + said (chosen);
    (void) places();
    if (to == cur && ! gPlaces.libraryMissing) { gPlaces.next = {}; writePlaces(); return "THE LIBRARY IS THERE ALREADY"; }
    if (to.isAChildOf (cur.getChildFile ("samples")) || to.isAChildOf (cur.getChildFile ("clips"))) return "THAT IS INSIDE THE LIBRARY";
    const bool there = to.getChildFile ("session.json").existsAsFile();
    gPlaces.next = to == homeDir() ? juce::String ("(home)") : to.getFullPathName();
    writePlaces();
    return "FROM THE NEXT START: " + said (to) + (there ? ". THE LIBRARY ALREADY IN IT IS USED." : ". THIS LIBRARY IS COPIED THERE.") + " QUIT AND OPEN SF-000 AGAIN.";
}

juce::String Session::chooseRecordings (const juce::File& chosen)
{
    if (stateFileForTests != juce::File()) return {};
    if (! chosen.isDirectory()) return "THAT IS NOT A FOLDER";
    if (! chosen.hasWriteAccess()) return "SF-000 CANNOT WRITE IN " + said (chosen);
    (void) places();
    gPlaces.recordings = chosen == usualRecordingsDir() ? juce::String() : chosen.getFullPathName();
    writePlaces();
    return {};
}

juce::File Session::stateFile()
{
    if (stateFileForTests != juce::File()) return stateFileForTests;
    return libraryDir().getChildFile ("session.json");
}

// ------------------------------------------------------------------------------- the samples
juce::String Session::refOf (const juce::String& full)
{
    if (full.isEmpty() || ! juce::File::isAbsolutePath (full)) return full;
    const juce::File f (full), lib = libraryDir();
    return f.isAChildOf (lib) ? f.getRelativePathFrom (lib).replaceCharacter ('\\', '/') : full;
}

juce::String Session::pathOf (const juce::String& ref) const
{
    if (ref.isEmpty()) return {};
    if (! juce::File::isAbsolutePath (ref)) return libraryDir().getChildFile (ref).getFullPathName();
    // written whole (by a build from before copies were kept): its copy, if one was made from it
    for (auto& s : samples)
        if (s.from == ref)
            if (const auto c = libraryDir().getChildFile (s.file); c.existsAsFile()) return c.getFullPathName();
    return ref;
}

juce::String Session::keepSample (const juce::String& path)
{
    if (! useDisk || path.isEmpty() || ! juce::File::isAbsolutePath (path)) return path;
    const juce::File f (path), dir = libraryDir().getChildFile ("samples");
    if (f.isAChildOf (dir) || ! f.existsAsFile()) return path;                 // one of the library's own already (or nothing to copy)
    // Under its own name, in a folder named for what is in it (the first eight digits of its
    // SHA-256): the same file given twice is kept once, and two files of one name do not collide.
    const juce::String hash = juce::SHA256 (f).toHexString();
    if (hash.isEmpty()) return path;
    juce::String name = juce::File::createLegalFileName (f.getFileName());
    if (name.isEmpty()) name = "sample" + f.getFileExtension();
    const juce::File in = dir.getChildFile (hash.substring (0, 8)), copy = in.getChildFile (name);
    if (! copy.existsAsFile())
    {
        if (! in.createDirectory() || ! f.copyFileTo (copy)) return path;      // (no room, or it cannot be written: read from where it is)
    }
    const juce::String rel = "samples/" + hash.substring (0, 8) + "/" + name;
    bool listed = false;
    for (auto& s : samples) listed = listed || (s.file == rel && s.from == path);
    if (! listed) { samples.push_back ({ rel, path, hash, copy.getSize() }); dirty = true; }
    samplesMoved = true;                // (what this one took the place of may be nobody's now: looked at on the next tick)
    return copy.getFullPathName();
}

juce::String Session::cameFrom (const juce::String& full) const
{
    for (auto& s : samples) if (libraryDir().getChildFile (s.file).getFullPathName() == full && s.from.isNotEmpty()) return s.from;
    return full;
}

int Session::sweepSamples()
{
    if (! useDisk || samples.empty()) return 0;
    // every file something still refers to: the rig as it stands, and every snapshot
    juce::StringArray used;
    auto use = [&] (const juce::String& full) { if (full.isNotEmpty()) used.addIfNotAlreadyThere (juce::File (full).getFullPathName()); };
    auto useRefs = [&] (const std::vector<Engine::FileRef>& refs) { for (auto& r : refs) for (auto& f : r.files) use (f); };
    for (int l = 0; l < kLanes; ++l)
    {
        use (engine.laneFile (l));
        for (int k = 0; k < kSlots; ++k) useRefs (engine.heldFiles ({ 1, l, k }));
    }
    useRefs (engine.heldFiles ({ 2, 0, 1 }));
    for (auto& s : library)
        for (int l = 0; l < kLanes; ++l)
        {
            use (s.laneFiles[(size_t) l]);
            for (int k = 0; k < kSlots; ++k) useRefs (s.insFiles[(size_t) l][(size_t) k]);
        }
    int gone = 0;
    std::vector<Sample> keep;
    for (auto& s : samples)
    {
        const juce::File copy = libraryDir().getChildFile (s.file);
        if (used.contains (copy.getFullPathName())) { keep.push_back (s); continue; }
        if (! copy.existsAsFile()) continue;                    // (listed again from another place, and gone a moment ago; or never there)
        // If the file it was copied from is still where it was, nothing is lost and the copy is
        // simply deleted. If not, this may be the last there is of it: to the Trash.
        bool elsewhere = false;
        for (auto& o : samples) if (o.file == s.file) if (const juce::File was (o.from); was.existsAsFile() && was.getSize() == copy.getSize()) elsewhere = true;
        if (elsewhere || ! copy.moveToTrash()) copy.deleteFile();
        if (const auto in = copy.getParentDirectory(); in.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0) in.deleteFile();
        ++gone;
    }
    if (keep.size() != samples.size()) { samples = keep; dirty = true; }
    return gone;
}

static var patternToVar (const Pattern& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("len", p.length);
    juce::Array<var> st;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& s = p.steps[(size_t) i];
        if (! s.on) continue;
        juce::Array<var> a;
        a.add (i); a.add (s.vel); a.add (s.gate); a.add (s.ratchet); a.add (s.note); a.add (s.octave);
        st.add (var (a));
    }
    o->setProperty ("steps", st);
    return var (o);
}

static Pattern patternFromVar (const var& v, int octave)
{
    Pattern p;
    for (auto& s : p.steps) s.octave = octave;
    p.length = juce::jlimit (1, kMaxSteps, (int) v.getProperty ("len", 16));
    if (auto* st = v.getProperty ("steps", var()).getArray())
        for (auto& e : *st)
            if (auto* a = e.getArray(); a != nullptr && a->size() >= 6)
            {
                const int i = (int) (*a)[0];
                if (i < 0 || i >= kMaxSteps) continue;
                auto& s = p.steps[(size_t) i];
                s.on = true; s.vel = (float) (*a)[1]; s.gate = (float) (*a)[2];
                s.ratchet = juce::jlimit (1, 4, (int) (*a)[3]); s.note = juce::jlimit (0, 11, (int) (*a)[4]); s.octave = juce::jlimit (0, 8, (int) (*a)[5]);
            }
    return p;
}

static var setupToVar (const Engine::SetupState& st)
{
    auto* o = new juce::DynamicObject();
    for (auto& kv : st) o->setProperty (kv.first, kv.second);
    return var (o);
}

static var refsToVar (const std::vector<Engine::FileRef>& refs)
{
    juce::Array<var> a;
    for (auto& r : refs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("place", r.place);
        o->setProperty ("type", r.type);
        juce::Array<var> fl;
        for (auto& f : r.files) fl.add (Session::refOf (f));    // (a copy in the library: from its folder down)
        o->setProperty ("files", fl);
        a.add (var (o));
    }
    return a;
}

static std::vector<Engine::FileRef> refsFromVar (const var& v)
{
    std::vector<Engine::FileRef> refs;
    if (auto* a = v.getArray())
        for (auto& e : *a)
        {
            Engine::FileRef r;
            r.place = (int) e.getProperty ("place", 0);
            r.type = (int) e.getProperty ("type", -1);
            if (auto* fl = e.getProperty ("files", var()).getArray()) for (auto& f : *fl) r.files.add (f.toString());
            if (! r.files.isEmpty()) refs.push_back (r);
        }
    return refs;
}

static Engine::SetupState setupFromVar (const var& v)
{
    Engine::SetupState st;
    if (auto* o = v.getDynamicObject())
        for (auto& p : o->getProperties()) st.push_back ({ p.name.toString(), juce::jlimit (0.0f, 1.0f, (float) p.value) });
    return st;
}

static var snapToVar (const Snapshot& s, const Engine& e)
{
    if (s.kept.isObject()) return s.kept;           // as it was read: nothing in it is lost (Session.h)
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", s.id);
    if (s.uid.isNotEmpty()) o->setProperty ("uid", s.uid);
    o->setProperty ("name", s.name);
    o->setProperty ("time", s.time.toMilliseconds());
    if (! s.made.isVoid()) o->setProperty ("made", s.made);
    if (s.profile.has) o->setProperty ("profile", s.profile.toVar());
    auto* vals = new juce::DynamicObject();
    for (int i = 0; i < cx::kCount && i < (int) s.values.size(); ++i)
        if (s.values[(size_t) i] >= 0.0f) vals->setProperty (e.control (i).id, s.values[(size_t) i]);
    o->setProperty ("values", var (vals));
    juce::Array<var> pats, ins, gen;
    for (int l = 0; l < kLanes; ++l)
    {
        pats.add (patternToVar (s.patterns[(size_t) l]));
        juce::Array<var> sl;
        for (int k = 0; k < kSlots; ++k) sl.add (s.inserts[(size_t) l][(size_t) k] >= 0 ? var (effects()[(size_t) s.inserts[(size_t) l][(size_t) k]].name) : var ("NONE"));
        ins.add (var (sl));
        gen.add (s.gen[(size_t) l]);
    }
    o->setProperty ("patterns", pats);
    o->setProperty ("inserts", ins);
    o->setProperty ("gen", gen);
    if (s.hasInsFiles)
    {
        juce::Array<var> insf;
        for (int l = 0; l < kLanes; ++l)
            for (int k = 0; k < kSlots; ++k)
                if (const auto& refs = s.insFiles[(size_t) l][(size_t) k]; ! refs.empty())
                {
                    auto* f = new juce::DynamicObject();
                    f->setProperty ("lane", l);
                    f->setProperty ("slot", k);
                    f->setProperty ("files", refsToVar (refs));
                    insf.add (var (f));
                }
        o->setProperty ("insertFiles", insf);
    }
    if (s.hasSetup)
    {
        juce::Array<var> ls, is, rs;
        for (int l = 0; l < kLanes; ++l)
        {
            ls.add (setupToVar (s.setup[(size_t) l]));
            juce::Array<var> sl;
            for (int k = 0; k < kSlots; ++k) sl.add (setupToVar (s.insSetup[(size_t) l][(size_t) k]));
            is.add (var (sl));
        }
        for (int r = 0; r < 2; ++r) rs.add (setupToVar (s.retSetup[(size_t) r]));
        auto* su = new juce::DynamicObject();
        su->setProperty ("lanes", ls); su->setProperty ("inserts", is); su->setProperty ("returns", rs);
        if (s.hasBus) su->setProperty ("bus", setupToVar (s.busSetup));
        o->setProperty ("setup", var (su));
    }
    if (s.hasPlayed)
    {
        // what was playing that the lanes' own steps do not say
        juce::Array<var> mutes, in;
        for (int l = 0; l < kLanes; ++l) mutes.add (s.seqMuted[(size_t) l] ? 1 : 0);
        for (int l = 0; l < kLanes; ++l)
            if (s.heardOn[(size_t) l])
            {
                var e = patternToVar (s.heard[(size_t) l]);
                e.getDynamicObject()->setProperty ("lane", l);
                if (s.heardNotes[(size_t) l].isArray()) e.getDynamicObject()->setProperty ("notes", s.heardNotes[(size_t) l]);
                in.add (e);
            }
        auto* pl = new juce::DynamicObject();
        pl->setProperty ("seqMute", mutes); pl->setProperty ("in", in);
        o->setProperty ("played", var (pl));
    }
    if (s.hasFiles)
    {
        juce::Array<var> fl;
        for (int l = 0; l < kLanes; ++l) fl.add (Session::refOf (s.laneFiles[(size_t) l]));
        o->setProperty ("laneFiles", fl);
    }
    return var (o);
}

juce::var Session::written (const Snapshot& s) const { return snapToVar (s, engine); }

juce::var Session::definition (const juce::String& print) const
{
    for (auto& e : engine.defs().all) if (e.first == print) return e.second;
    for (auto& e : defsKept) if (e.first == print) return e.second;
    return {};
}

static Snapshot snapFromVar (const var& v, const Engine& e)
{
    Snapshot s;
    s.kept = v;
    s.id = (juce::int64) v.getProperty ("id", 0);
    s.uid = v.getProperty ("uid", "").toString();
    s.made = v.getProperty ("made", var());
    s.name = v.getProperty ("name", "SNAP").toString();
    s.time = juce::Time ((juce::int64) v.getProperty ("time", 0));
    s.profile = Profile::fromVar (v.getProperty ("profile", var()));
    s.values.assign ((size_t) cx::kCount, -1.0f);
    // by id: a control that no longer exists is ignored, a new one keeps its place in the base
    if (auto* vals = v.getProperty ("values", var()).getDynamicObject())
        for (auto& p : vals->getProperties())
        {
            const int i = e.find (p.name.toString());
            if (i >= 0) s.values[(size_t) i] = juce::jlimit (0.0f, 1.0f, (float) p.value);
        }
    const auto pats = v.getProperty ("patterns", var()), ins = v.getProperty ("inserts", var()), gen = v.getProperty ("gen", var());
    for (int l = 0; l < kLanes; ++l)
    {
        const int oct = lanes()[(size_t) l].octave;
        s.patterns[(size_t) l] = l < pats.size() ? patternFromVar (pats[l], oct) : Engine::defaultPattern (l);
        for (int k = 0; k < kSlots; ++k)
        {
            s.inserts[(size_t) l][(size_t) k] = -1;
            if (l < ins.size() && k < ins[l].size())
            {
                const auto nm = ins[l][k].toString();
                for (size_t f = 0; f < effects().size(); ++f) if (nm == effects()[f].name) s.inserts[(size_t) l][(size_t) k] = (int) f;
            }
        }
        s.gen[(size_t) l] = l < gen.size() ? juce::jlimit (-1, 1, (int) gen[l]) : -1;
    }
    if (const var su = v.getProperty ("setup", var()); su.isObject())
    {
        const var ls = su.getProperty ("lanes", var()), is = su.getProperty ("inserts", var()), rs = su.getProperty ("returns", var());
        for (int l = 0; l < kLanes; ++l)
        {
            if (l < ls.size()) s.setup[(size_t) l] = setupFromVar (ls[l]);
            for (int k = 0; k < kSlots; ++k) if (l < is.size() && k < is[l].size()) s.insSetup[(size_t) l][(size_t) k] = setupFromVar (is[l][k]);
        }
        for (int r = 0; r < 2; ++r) if (r < rs.size()) s.retSetup[(size_t) r] = setupFromVar (rs[r]);
        s.hasSetup = true;
        if (const var bs = su.getProperty ("bus", var()); bs.isObject()) { s.busSetup = setupFromVar (bs); s.hasBus = true; }
    }
    if (const var insf = v.getProperty ("insertFiles", var()); insf.isArray())
    {
        for (int n = 0; n < insf.size(); ++n)
        {
            const int l = (int) insf[n].getProperty ("lane", -1), k = (int) insf[n].getProperty ("slot", -1);
            if (l < 0 || l >= kLanes || k < 0 || k >= kSlots || s.inserts[(size_t) l][(size_t) k] < 0) continue;
            auto refs = refsFromVar (insf[n].getProperty ("files", var()));
            for (auto& r : refs) r.type = s.inserts[(size_t) l][(size_t) k];       // the effect is kept by name: its files go with it
            s.insFiles[(size_t) l][(size_t) k] = refs;
        }
        s.hasInsFiles = true;
    }
    if (const var pl = v.getProperty ("played", var()); pl.isObject())
    {
        const var mutes = pl.getProperty ("seqMute", var()), in = pl.getProperty ("in", var());
        for (int l = 0; l < kLanes; ++l) s.seqMuted[(size_t) l] = l < mutes.size() && (int) mutes[l] != 0;
        for (int n = 0; n < in.size(); ++n)
        {
            const int l = (int) in[n].getProperty ("lane", -1);
            if (l < 0 || l >= kLanes) continue;
            s.heard[(size_t) l] = patternFromVar (in[n], lanes()[(size_t) l].octave);
            s.heardNotes[(size_t) l] = in[n].getProperty ("notes", var());
            s.heardOn[(size_t) l] = true;
        }
        s.hasPlayed = true;
    }
    if (const var fl = v.getProperty ("laneFiles", var()); fl.isArray())
    {
        for (int l = 0; l < kLanes && l < fl.size(); ++l) s.laneFiles[(size_t) l] = fl[l].toString();
        s.hasFiles = true;
    }
    return s;
}

void Session::save()
{
    dirty = false;
    if (notSavingWhy.isNotEmpty()) return;          // a newer build's file is on the disk: it stays as it is
    const var root = stateVar();
    const auto f = stateFile();
    f.getParentDirectory().createDirectory();
    const auto tmp = f.getSiblingFile ("session.tmp");
    if (tmp.replaceWithText (juce::JSON::toString (root))) tmp.moveFileTo (f);
}

// Everything the session file holds, as it stands now (the file is this, written out; a print's
// own rig is made from it).
var Session::stateVar()
{
    sign();
    auto* root = new juce::DynamicObject();
    root->setProperty ("sf000", kFormat);
    {
        auto* w = new juce::DynamicObject();
        w->setProperty ("app", juce::String (JUCE_APPLICATION_VERSION_STRING) + " " + SF000_COMMIT);
        w->setProperty ("at", juce::Time::currentTimeMillis());
        root->setProperty ("wrote", var (w));
    }
    root->setProperty ("libraryId", libraryId);

    Snapshot b = capture();
    for (int i = 0; i < kMorphEnd; ++i)
        if (engine.control (i).morph && b.values[(size_t) i] >= 0.0f) b.values[(size_t) i] = juce::jlimit (0.0f, 1.0f, base[(size_t) i]);
    b.name = "BASE";
    b.kept = var(); b.uid.clear();
    root->setProperty ("base", snapToVar (b, engine));
    root->setProperty ("baseId", baseId);

    auto* other = new juce::DynamicObject();        // what a snapshot leaves alone: tempo, glide, master, the snapshot knobs
    for (int i = 0; i < cx::kCount; ++i)
        if (engine.control (i).used && ! engine.control (i).snapshot) other->setProperty (engine.control (i).id, engine.target (i));
    for (auto& m : controlsMore) if (! other->hasProperty (m.name)) other->setProperty (m.name, m.value);
    root->setProperty ("controls", var (other));
    root->setProperty ("activeLane", activeLane);
    root->setProperty ("recallNotes", recallNotes);
    juce::Array<var> fl;
    for (int l = 0; l < kLanes; ++l) fl.add (refOf (engine.laneFile (l)));
    root->setProperty ("files", fl);
    juce::Array<var> sm;                             // the copies of samples this library made, and where each came from
    for (auto& x : samples)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("file", x.file); o->setProperty ("from", x.from); o->setProperty ("sha256", x.hash); o->setProperty ("bytes", x.bytes);
        sm.add (var (o));
    }
    root->setProperty ("samples", sm);
    // SPACE: which reverb it is, and the impulse responses its convolution was given. An insert's too.
    auto* sp = new juce::DynamicObject();
    sp->setProperty ("model", engine.spaceModel());
    sp->setProperty ("files", refsToVar (engine.heldFiles ({ 2, 0, 1 })));
    root->setProperty ("space", var (sp));
    juce::Array<var> insf;
    for (int l = 0; l < kLanes; ++l)
        for (int k = 0; k < kSlots; ++k)
            if (const auto& held = engine.heldFiles ({ 1, l, k }); ! held.empty())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("lane", l);
                o->setProperty ("slot", k);
                o->setProperty ("files", refsToVar (held));
                insf.add (var (o));
            }
    root->setProperty ("insertFiles", insf);

    juce::Array<var> lib, kn;
    for (auto& s : library) lib.add (snapToVar (s, engine));
    for (auto& k : knobs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", k.name);
        o->setProperty ("depth", k.depth);
        juce::Array<var> ids;
        for (auto id : k.snaps) ids.add (id);
        o->setProperty ("snaps", ids);
        if (! k.links.empty() || k.linksKept.size() > 0)
        {
            // the knobs it is tied to: each by its id, and how far the full turn takes it (FORMAT.md)
            juce::Array<var> ls;
            for (const auto& l : k.links)
            {
                auto* lo = new juce::DynamicObject();
                lo->setProperty ("c", engine.control (l.ctl).id);
                lo->setProperty ("d", l.amount);
                ls.add (var (lo));
            }
            for (auto& v : k.linksKept) ls.add (v);
            o->setProperty ("links", ls);
        }
        for (auto& m : k.more) o->setProperty (m.name, m.value);
        kn.add (var (o));
    }
    root->setProperty ("library", lib);
    root->setProperty ("knobs", kn);
    if (! arrangement.empty() || arrMore.size() > 0) root->setProperty ("arrangement", arrToVar());
    // The definitions: every one the file has held, and this build's own. Never pruned: a snapshot
    // made long ago still finds what it was made with.
    {
        auto* d = new juce::DynamicObject();
        for (auto& e : defsKept) d->setProperty (e.first, e.second);
        for (auto& e : engine.defs().all) if (! d->hasProperty (e.first)) d->setProperty (e.first, e.second);
        root->setProperty ("defs", var (d));
    }
    for (auto& m : rootMore) if (! root->hasProperty (m.name)) root->setProperty (m.name, m.value);
    return var (root);
}

void Session::backUp()
{
    // The session as this launch found it, before anything writes to it: backups/session-<date>.json.
    // A bad build or a bad save then costs at most what was done since the last time the app opened.
    const auto f = stateFile();
    if (! f.existsAsFile()) return;
    const auto dir = f.getSiblingFile ("backups");
    if (! dir.createDirectory()) return;
    f.copyFileTo (dir.getChildFile ("session-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".json"));
    auto old = dir.findChildFiles (juce::File::findFiles, false, "session-*.json");
    old.sort();                                                 // by name: oldest first
    for (int i = 0; i < old.size() - 20; ++i) old[i].deleteFile();
}

bool Session::load()
{
    const auto f = stateFile();
    if (! f.existsAsFile()) return false;
    backUp();
    const var root = juce::JSON::parse (f);
    const int format = root.isObject() ? (int) root.getProperty ("sf000", 0) : 0;
    if (format > kFormat)
    {
        // From a newer SF-000, in a format this build cannot read. It is left exactly as it is, and
        // nothing is written this time, so going back to the newer build finds everything in place.
        notSavingWhy = "This session was saved by a newer SF-000 (format " + juce::String (format) + "; this build reads format " + juce::String (kFormat)
                       + "). It has been left exactly as it is, and nothing will be saved this time: snapshots taken now are gone when SF-000 closes. Open it with the newer build.";
        useDisk = false;
        return false;
    }
    if (format < 1)
    {
        // Not a session (cut short, or something else). It is about to be replaced by a fresh one:
        // a copy is put aside first, and is not one of the twenty backups that come and go.
        if (f.getSize() > 0) f.copyFileTo (f.getSiblingFile ("session-unreadable-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".json"));
        return false;
    }
    takeState (root);
    return true;
}

// The session made what a file's contents say (see stateVar).
void Session::takeState (const var& root)
{
    // What this build does not know is carried, not dropped: the next save writes it back.
    static const char* const known[] = { "sf000", "wrote", "libraryId", "defs", "base", "baseId", "controls", "activeLane", "files", "space", "insertFiles", "library", "knobs", "recallNotes", "samples", "arrangement" };
    rootMore.clear(); controlsMore.clear(); defsKept.clear();
    for (auto& p : root.getDynamicObject()->getProperties())
    {
        bool is = false;
        for (auto* k : known) is = is || p.name == juce::Identifier (k);
        if (! is) rootMore.set (p.name, p.value);
    }
    if (auto* d = root.getProperty ("defs", var()).getDynamicObject())
        for (auto& p : d->getProperties()) defsKept.push_back ({ p.name.toString(), p.value });
    idInFile = root.getProperty ("libraryId", "").toString();

    learning = -1; learnBase.clear();
    library.clear();
    if (auto* lib = root.getProperty ("library", var()).getArray())
        for (auto& v : *lib) library.push_back (snapFromVar (v, engine));
    arrFromVar (root.getProperty ("arrangement", var()));
    if (auto* kn = root.getProperty ("knobs", var()).getArray())
        for (int k = 0; k < 16 && k < kn->size(); ++k)
        {
            const juce::var o = (*kn)[k];
            knobs[(size_t) k].name = o.getProperty ("name", "").toString();
            knobs[(size_t) k].depth = juce::jlimit (0.0f, 2.0f, (float) o.getProperty ("depth", 1.0));
            knobs[(size_t) k].snaps.clear();
            if (auto* ids = o.getProperty ("snaps", var()).getArray())
                for (auto& id : *ids) if (findSnap ((juce::int64) id) != nullptr) knobs[(size_t) k].snaps.push_back ((juce::int64) id);
            knobs[(size_t) k].links.clear();
            knobs[(size_t) k].linksKept.clear();
            if (auto* ls = o.getProperty ("links", var()).getArray())
                for (auto& l : *ls)
                {
                    const int i = engine.find (l.getProperty ("c", "").toString());
                    const float d = juce::jlimit (-1.0f, 1.0f, (float) (double) l.getProperty ("d", 0.0));
                    if (reaches (engine, i)) { if (std::abs (d) >= 0.004f) knobs[(size_t) k].links.push_back ({ i, d }); }
                    else knobs[(size_t) k].linksKept.add (l);       // (a knob this build does not have, or cannot push: carried)
                }
            knobs[(size_t) k].more.clear();
            if (auto* ko = o.getDynamicObject())
                for (auto& p : ko->getProperties())
                    if (p.name != juce::Identifier ("name") && p.name != juce::Identifier ("depth") && p.name != juce::Identifier ("snaps") && p.name != juce::Identifier ("links")) knobs[(size_t) k].more.set (p.name, p.value);
        }
    if (auto* other = root.getProperty ("controls", var()).getDynamicObject())
        for (auto& p : other->getProperties())
        {
            const int i = engine.find (p.name.toString());
            if (i >= 0 && ! engine.control (i).snapshot) { engine.set (i, (float) p.value); base[(size_t) i] = engine.target (i); }
            if (i < 0) controlsMore.set (p.name, p.value);
        }
    activeLane = juce::jlimit (0, kLanes - 1, (int) root.getProperty ("activeLane", 7));
    recallNotes = (bool) root.getProperty ("recallNotes", true);
    // The samples. What is written is turned into a path to read: a copy in the library by where
    // the library is now; a file written whole (by a build from before copies were kept) is
    // copied in now if it is still where it was, and read from its copy from here on.
    samples.clear();
    if (auto* sm = root.getProperty ("samples", var()).getArray())
        for (auto& e : *sm)
            if (const juce::String file = e.getProperty ("file", var()).toString(); file.isNotEmpty())
                samples.push_back ({ file, e.getProperty ("from", var()).toString(), e.getProperty ("sha256", var()).toString(), (juce::int64) e.getProperty ("bytes", 0) });
    auto found = [this] (const juce::String& ref)
    {
        if (ref.isNotEmpty() && juce::File::isAbsolutePath (ref))
        {
            bool copied = false;
            for (auto& x : samples) copied = copied || x.from == ref;
            if (! copied) return keepSample (ref);
        }
        return pathOf (ref);
    };
    auto foundRefs = [&found] (std::vector<Engine::FileRef> refs) { for (auto& r : refs) for (auto& f : r.files) f = found (f); return refs; };
    auto foundIn = [&] (Snapshot& x)
    {
        for (auto& f : x.laneFiles) f = found (f);
        for (auto& lane : x.insFiles) for (auto& slot : lane) slot = foundRefs (slot);
    };
    for (auto& x : library) foundIn (x);
    if (const var fl = root.getProperty ("files", var()); fl.isArray())
        for (int l = 0; l < kLanes && l < fl.size(); ++l)
            if (const juce::File f (found (fl[l].toString())); fl[l].toString().isNotEmpty() && f.existsAsFile()) engine.loadFile (l, f);
    if (const var sp = root.getProperty ("space", var()); sp.isObject())
    {
        engine.setSpaceModel ((int) sp.getProperty ("model", 0));
        engine.setHeldFiles ({ 2, 0, 1 }, foundRefs (refsFromVar (sp.getProperty ("files", var()))));
    }
    if (auto* insf = root.getProperty ("insertFiles", var()).getArray())
        for (auto& e : *insf)
        {
            const int l = (int) e.getProperty ("lane", -1), k = (int) e.getProperty ("slot", -1);
            if (l >= 0 && l < kLanes && k >= 0 && k < kSlots) engine.setHeldFiles ({ 1, l, k }, foundRefs (refsFromVar (e.getProperty ("files", var()))));
        }
    baseId = (juce::int64) root.getProperty ("baseId", 0);
    selStep = -1;
    if (root.hasProperty ("base"))
    {
        Snapshot was = snapFromVar (root["base"], engine);
        foundIn (was);
        applySnapshot (was, 0.0);
    }
    motionLaws();
    warmInserts();
}

void Session::warmInserts()
{
    // A snapshot recall should not wait on the disk: the file-holding inserts the library names
    // are made now, with their files read, and kept aside until a recall wants them.
    for (auto& s : library)
        if (s.hasInsFiles)
            for (int l = 0; l < kLanes; ++l)
                for (int k = 0; k < kSlots; ++k)
                    if (const auto& refs = s.insFiles[(size_t) l][(size_t) k]; ! refs.empty())
                        engine.warmInsert (l, k, s.inserts[(size_t) l][(size_t) k], refs);
}

// ------------------------------------------------------------------------- what it starts with
void Session::ensureCleanSlate()
{
    // CLEAN SLATE: every knob where the rig starts it, no steps, no inserts. Always in the library,
    // and made again each time the app opens, so it covers whatever a newer build added.
    Snapshot s;
    s.id = kCleanSlateId;
    s.name = "CLEAN SLATE";
    s.time = juce::Time::getCurrentTime();
    s.values.assign ((size_t) cx::kCount, -1.0f);
    for (int i = 0; i < cx::kCount; ++i)
    {
        const auto& c = engine.control (i);
        const int k = i < cx::kRet ? i % cx::kPerLane : -1;
        const bool insertCell = k >= cx::kIns && k < cx::kGen;
        if (c.snapshot && c.used && ! insertCell) s.values[(size_t) i] = c.def;
    }
    for (int l = 0; l < kLanes; ++l)
    {
        for (auto& st : s.patterns[(size_t) l].steps) st.octave = lanes()[(size_t) l].octave;
        s.inserts[(size_t) l].fill (-1);
        s.gen[(size_t) l] = -1;
        s.setup[(size_t) l] = engine.initialSetup (l);
    }
    s.retSetup = retInit;
    s.busSetup = busInit;
    s.hasSetup = true;
    s.hasBus = true;
    s.hasInsFiles = true;
    const int at = indexOf (kCleanSlateId);
    s.made = madeNow (s);
    if (at >= 0) { s.name = library[(size_t) at].name; s.time = library[(size_t) at].time; s.uid = library[(size_t) at].uid; library[(size_t) at] = s; }
    else { library.insert (library.begin(), s); dirty = true; }
}

void Session::makeFactory()
{
    // The rig as it comes up is INIT. Five more snapshots, each a push in one direction from it,
    // and a snapshot knob for each: turning one adds its push to wherever the rig is.
    auto init = capture();
    init.name = "INIT";
    init.id = 1000;
    library.push_back (init);
    struct Mod { const char* id; float v; };
    auto derive = [&] (juce::int64 id, const char* name, std::initializer_list<Mod> mods)
    {
        Snapshot s = init;
        s.id = id; s.name = name;
        for (auto& m : mods)
        {
            const int i = engine.find (m.id);
            jassert (i >= 0);
            if (i >= 0) s.values[(size_t) i] = m.v;
        }
        library.push_back (s);
    };
    derive (1001, "ENERGY UP", { { "closed_hat.density", .45f }, { "closed_hat.decay", .45f }, { "closed_hat.level", .64f }, { "open_hat.density", .3f },
                                 { "perc.density", .4f }, { "bass.cutoff", .5f }, { "bass.env_amt", .6f }, { "lead.cutoff", .55f }, { "lead.density", .3f },
                                 { "chord.cutoff", .6f }, { "kick.punch", .6f }, { "clap.send_b", .5f }, { "rumble.level", .68f } });
    derive (1002, "BUILD TOP", { { "closed_hat.filter", .7f }, { "open_hat.filter", .7f }, { "clap.filter", .66f }, { "perc.filter", .7f }, { "bass.filter", .74f },
                                 { "rumble.filter", .78f }, { "lead.filter", .68f }, { "chord.filter", .68f }, { "pad.filter", .68f }, { "voice.filter", .66f },
                                 { "kick.filter", .62f }, { "lead.send_a", .6f }, { "chord.send_b", .6f }, { "clap.send_b", .6f }, { "closed_hat.density", .6f },
                                 { "ret.space_decay", .62f }, { "ret.dub_feedback", .68f } });
    derive (1003, "ACID OPEN", { { "lead.res", .92f }, { "lead.env_amt", .85f }, { "lead.cutoff", .55f }, { "lead.drive", .45f }, { "lead.accent", .9f },
                                 { "lead.decay", .65f }, { "bass.res", .6f }, { "bass.env_amt", .65f } });
    derive (1004, "DIST HOT",  { { "kick.drive", .6f }, { "bass.drive", .55f }, { "lead.drive", .7f }, { "perc.power", .8f }, { "closed_hat.drive", .5f },
                                 { "open_hat.drive", .5f }, { "clap.drive", .5f }, { "rumble.grit", .7f }, { "voice.drive", .6f }, { "ret.dub_drive", .7f } });
    derive (1005, "WARP FAR",  { { "pad.detune", 1.0f }, { "pad.color", .9f }, { "chord.detune", .9f }, { "perc.size", .12f }, { "perc.tension", .9f },
                                 { "perc.material", .85f }, { "lead.color", .9f }, { "kick.pitch_time", .95f }, { "kick.tune", .3f }, { "closed_hat.tune", .2f },
                                 { "open_hat.tune", .75f }, { "voice.tune", .25f }, { "rumble.size", 1.0f }, { "bass.glide", .7f } });
    static const char* const names[] = { "ENERGY", "BUILD", "ACID", "DIST", "WARP" };
    for (int k = 0; k < 5; ++k) { knobs[(size_t) k].name = names[k]; knobs[(size_t) k].snaps = { 1000, 1001 + k }; }
    baseId = 1000;
}
} // namespace sf0
