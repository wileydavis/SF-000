#include "Session.h"
#include <map>
#include "Build.h"
#include <juce_cryptography/juce_cryptography.h>
#include "core/Sample.h"

namespace sf0
{
using juce::var;

static const int kMorphEnd = cx::kRetMS;      // lanes, returns and the drum bus: what a snapshot knob can reach

juce::String Session::madeBy() { return juce::String (JUCE_APPLICATION_VERSION_STRING) + " " + SF000_COMMIT; }
juce::String Session::hashOf (const juce::String& text) { return juce::SHA256 (text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString(); }
juce::String Session::hashOf (const void* data, size_t bytes) { return juce::SHA256 (data, bytes).toHexString(); }

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
    modulesReady();
    arrsReady();
    findLibraryId (idInFile);
    sign();
    if (useDisk) startTimer (3000);
}

Session::~Session()
{
    stopTimer();
    // (a print still going when the app shuts: its rig and its part-made files go, and the bus has its clock back)
    if (print != nullptr) { onChange = nullptr; audioResume = nullptr; printCancel(); }
    finishTakes (true);
    engine.dropCue();                   // before the clips go
    waitForClips();
    waitForLog();
    if (useDisk) { save(); engine.keepFile = nullptr; }
}

void Session::timerCallback()
{
    if (printRig) return;                           // (a print's own rig: the print does its looking)
    // The arrangement, while it plays: looked at often, for the part that is next. The rest of
    // what this does is slower work, done about once a second meanwhile.
    const bool transWas = ! transPend.empty();
    if (transWas) transTick();                      // (what waits for a transition's line: looked at as often)
    if (arrOn) { arrTick(); if (arrOn && ++arrTicks % 64 != 0) return; }
    else if (transWas && ! transPend.empty() && ++arrTicks % 64 != 0) return;
    finishTakes();
    // (the log: the clock started or stopped. Seen when this looks, so to within three seconds)
    if (const bool run = engine.isRunning(); run != logRunWas) { logRunWas = run; logAction (run ? "run" : "stop", 0, { { "bpm", (double) engine.tempo() } }); }
    engine.tidy();
    pullHeard();
    if (samplesMoved) { samplesMoved = false; sweepSamples(); }      // a sample was given: the one it replaced, if nothing holds it now, goes
    // (the timer runs faster while a snapshot waits for its sound; the file is still written no more than every three seconds)
    if (const auto now = juce::Time::getMillisecondCounter(); useDisk && dirty && now - savedAt >= 2900) { savedAt = now; save(); }
    pruneCues();
    if (arrOn || ! transPend.empty()) { if (useDisk) startTimer (15); }
    else if (waiting.empty()) { if (useDisk) startTimer (3000); else stopTimer(); }
}

// ------------------------------------------------------------------------------------ FINISH
static const int kFinishPct[5] = { 0, 25, 50, 75, 100 };

int Session::finishPct() const { return kFinishPct[juce::jlimit (0, 4, finishAt)]; }
void Session::finishApply() { engine.setFinish ((float) finishPct() * 0.01f); }
void Session::finishSet (int place)
{
    finishAt = juce::jlimit (0, 4, place);
    finishApply();
    touch();
    if (onChange) onChange();
}
void Session::finishStep() { finishSet ((finishAt + 1) % 5); }
juce::String Session::finishSays() const { return finishAt == 0 ? juce::String ("FINISH OFF") : "FINISH " + juce::String (finishPct()); }

// ------------------------------------------------------------------------------------ TRANSITION
static const float kTransDb[6] = { -18.0f, -12.0f, -9.0f, -6.0f, -3.0f, 0.0f };
static const int kTransSend[6] = { 0, 15, 30, 50, 75, 100 };

void Session::transApply()
{
    engine.trans.level = juce::Decibels::decibelsToGain (kTransDb[juce::jlimit (0, 5, transLevelAt)]);
    engine.trans.toA = (float) kTransSend[juce::jlimit (0, 5, transDubAt)] * 0.01f;
    engine.trans.toB = (float) kTransSend[juce::jlimit (0, 5, transSpaceAt)] * 0.01f;
}

void Session::transFire()
{
    transApply();
    transAt = engine.transFire (transBars);
    if (onChange) onChange();
}

void Session::transArm()
{
    transArmed = ! transArmed;
    engine.trans.armed = transArmed;
    if (! transArmed && ! transPend.empty())
    {
        // put out with things waiting: they happen now
        engine.clearSetsAt();
        transDoing = true;
        {
            const Quiet q (*this);          // (each was logged when it was asked for)
            for (auto& p : transPend) { if (p.kind == 0) recall (p.id, p.scope); else userSet (p.ctl, p.v, p.sounding); }
        }
        transDoing = false;
        transPend.clear();
    }
    if (onChange) onChange();
}

double Session::transBeatsLeft() const
{
    if (transAt < 0.0 || ! engine.isRunning()) return 0.0;
    return std::max (0.0, transAt - engine.songPos());
}

bool Session::transDefer (TransPend p)
{
    if (transDoing || ! transArmed || printRig || ! engine.isRunning()) return false;
    // the first thing asked for sets the transition off (unless one is already on its way: then it is that one's line)
    if (transPend.empty() && transBeatsLeft() <= 0.0) { transApply(); transAt = engine.transFire (transBars); }
    if (transAt < 0.0) return false;
    // (a mute or a solo is given to the audio thread to make on the line, whatever this thread is doing then)
    if (p.kind == 1) p.onAudio = engine.setAt (p.ctl, p.v, transAt);
    transPend.push_back (std::move (p));
    if (useDisk) startTimer (15);
    if (onChange) onChange();
    return true;
}

void Session::transTick()
{
    if (transPend.empty()) return;
    const bool stopped = ! engine.isRunning();
    const double pos = engine.songPos();
    bool did = false;
    transDoing = true;
    for (size_t k = 0; k < transPend.size();)
    {
        // A recall sets off on the next beat, so it is made inside the beat before the line (as the arrangement makes its own);
        // a mute or a solo is at once, and is made a moment before the line, so that the line's own first hit is heard.
        const auto p = transPend[k];
        // (a recall: as soon as the line is the next beat. A mute or a solo the audio thread has: once the line is passed, it is
        // only written down here; one it has not, a moment before the line.)
        if (stopped || pos >= transAt - (p.kind == 0 ? 0.9 : p.onAudio ? 0.0 : 0.06))
        {
            transPend.erase (transPend.begin() + (std::ptrdiff_t) k);
            if (p.kind == 0) { glideOver = std::max (0.08, glideBeats()); recall (p.id, p.scope); glideOver = -1.0; }
            else userSet (p.ctl, p.v, p.onAudio && ! stopped && engine.target (p.ctl) == p.v);
            did = true;
        }
        else ++k;
    }
    transDoing = false;
    if (transPend.empty()) { transArmed = false; engine.trans.armed = false; }
    if (did && onChange) onChange();
}

void Session::transStep (int what)
{
    auto next = [this] (std::atomic<int>& kind, int slot)
    {
        // OFF, the built-in, the file (if one was given)
        const bool file = engine.transFile (slot).isNotEmpty();
        const int k = kind.load();
        kind = k == Transition::kOff ? Transition::kBuilt : k == Transition::kBuilt && file ? Transition::kFile : Transition::kOff;
    };
    switch (what)
    {
        case tBars:  transBars = transBars == 4 ? 8 : transBars == 8 ? 16 : 4; break;
        case tLevel: transLevelAt = (transLevelAt + 1) % 6; break;
        case tLead:  next (engine.trans.leadKind, 0); break;
        case tHit:   next (engine.trans.hitKind, 1); break;
        case tDub:   transDubAt = (transDubAt + 1) % 6; break;
        case tSpace: transSpaceAt = (transSpaceAt + 1) % 6; break;
        default: break;
    }
    transApply();
    touch();
    if (onChange) onChange();
}

juce::String Session::transSays (int what) const
{
    auto said = [this] (int kind, int slot, const char* built)
    {
        if (kind == Transition::kOff) return juce::String ("OFF");
        if (kind == Transition::kBuilt) return juce::String (built);
        return juce::File (engine.transFile (slot)).getFileNameWithoutExtension().toUpperCase().substring (0, 10);
    };
    switch (what)
    {
        case tBars:  return juce::String (transBars) + " BARS";
        case tLevel: return juce::String ((int) kTransDb[juce::jlimit (0, 5, transLevelAt)]) + " DB";
        case tLead:  return "IN " + said (engine.trans.leadKind.load(), 0, "RISER");
        case tHit:   return "HIT " + said (engine.trans.hitKind.load(), 1, "IMPACT");
        case tDub:   return "DUB " + juce::String (kTransSend[juce::jlimit (0, 5, transDubAt)]);
        case tSpace: return "SPACE " + juce::String (kTransSend[juce::jlimit (0, 5, transSpaceAt)]);
        default: return {};
    }
}

bool Session::transLoad (int slot, const juce::File& f)
{
    // (the library's own copy of it, as a lane's sample is: the set still has it when the file it came from has gone)
    if (! engine.transLoad (slot, juce::File (keepSample (f.getFullPathName())))) return false;
    transRef[(size_t) juce::jlimit (0, 1, slot)] = {};
    touch();
    if (onChange) onChange();
    return true;
}

var Session::transToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("bars", transBars); o->setProperty ("level", transLevelAt); o->setProperty ("dub", transDubAt); o->setProperty ("space", transSpaceAt);
    o->setProperty ("lead", engine.trans.leadKind.load()); o->setProperty ("hit", engine.trans.hitKind.load());
    // (a file by its place in the library; one that could not be found when the set opened is written back as it was read)
    for (int slot = 0; slot < 2; ++slot)
        o->setProperty (slot == 0 ? "leadFile" : "hitFile", engine.transFile (slot).isNotEmpty() ? refOf (engine.transFile (slot)) : transRef[(size_t) slot]);
    return var (o);
}

void Session::transFromVar (const var& v)
{
    if (v.isObject())
    {
        const int bars = (int) v.getProperty ("bars", 8);
        transBars = bars == 4 || bars == 16 ? bars : 8;
        transLevelAt = juce::jlimit (0, 5, (int) v.getProperty ("level", 3)); transDubAt = juce::jlimit (0, 5, (int) v.getProperty ("dub", 0)); transSpaceAt = juce::jlimit (0, 5, (int) v.getProperty ("space", 2));
        // (a file that is no longer there: the slot plays its built-in)
        for (int slot = 0; slot < 2; ++slot)
        {
            const juce::String path = v.getProperty (slot == 0 ? "leadFile" : "hitFile", "").toString();
            const int kind = juce::jlimit (0, 2, (int) v.getProperty (slot == 0 ? "lead" : "hit", 1));
            const bool has = path.isNotEmpty() && engine.transLoad (slot, juce::File (pathOf (path)));
            transRef[(size_t) slot] = has ? juce::String() : path;
            (slot == 0 ? engine.trans.leadKind : engine.trans.hitKind) = kind == Transition::kFile && ! has ? (int) Transition::kBuilt : kind;
        }
    }
    transApply();
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
    // CONFIG is one module's, and shuts when another comes to the front: another lane, by any hand; or this lane
    // from one of its inserts, the returns or the master, by a controller or the keyboard. (It used to go along to
    // the new lane, which left the E16 on CONFIG's page when the lane's own page was what had been asked for.
    // OPEN, there, opens the new one's.) The same lane brought again leaves it as it is.
    const bool anotherLane = l != activeLane, anotherModule = front.kind != fLane;
    if (cfgOpen && (anotherLane || (anotherModule && by != 0))) cfgOpen = false;
    if (l != activeLane) { activeLane = l; selStep = -1; page = 0; }
    front = { fLane, 0 };
    frontAt = juce::Time::getMillisecondCounter(); frontBy = by; ++frontCount;
    touch();
    if (onChange) onChange();
}

void Session::bring (Front f, int by)
{
    if (f.kind == fLane) { bringLane (activeLane, by); return; }
    f.slot = f.kind == fInsert ? juce::jlimit (0, kSlots - 1, f.slot) : 0;
    // (another module to the front by a controller or the keyboard: CONFIG shuts, as for another lane. Under the mouse
    // it stays where it was put. The same module brought again leaves it as it is.)
    if (cfgOpen && by != 0 && (f.kind != front.kind || f.slot != front.slot)) cfgOpen = false;
    front = f;
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
            cfgOpen = false;                    // (another lane's: CONFIG shuts)
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
    setupByHand (cfg, sp.id, v);
    cfgTouched = sp.id;
    touch();
    if (onChange) onChange();
}

void Session::setupByHand (Engine::Target t, const juce::String& id, float v)
{
    engine.setSetup (t, id, v);
    // (another model, with sound knobs of its own: the places it renamed start where those start, and that is where they stand)
    if (t.kind == 0 && id == "model") for (int k = 0; k < kSoundKnobs; ++k) { const int i = cx::lane (t.lane, k); base[(size_t) i] = engine.target (i) - offset[(size_t) i]; }
}

void Session::userSet (int i, float v, bool sounding)
{
    if (i < 0 || i >= cx::kCount) return;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (transArmed && ! transDoing)
        for (int l = 0; l < kLanes; ++l)
            if (i == cx::lane (l, cx::kMute) || i == cx::lane (l, cx::kSolo))
            { TransPend p; p.kind = 1; p.ctl = i; p.v = v; p.sounding = sounding; if (transDefer (p)) return; break; }      // (ARM: a mute or a solo waits for the line)
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
        if (k >= cx::kGen && k < cx::kGen + 4 && genType[(size_t) l] >= 0) regenerate (l);
    }
    touch();
}

void Session::turnSnapKnob (int k, float v)
{
    if (k < 0 || k >= 16 || isCorner (k)) return;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (! isStack (k))
    {
        if (k != learning) engine.set (cx::kSnap + k, v);   // (the knob being taught stands at its full turn)
        applyMorph (glideBeats());
        touch();
        return;
    }
    const float was = engine.target (cx::kSnap + k);
    auto& kn = knobs[(size_t) k];
    if (stackCue)
    {
        // CUE lit: the turn goes through the stack in the headphones (each snapshot with the parts it takes). The set is not touched.
        engine.set (cx::kSnap + k, v);
        const auto h = held (k);
        if (const int at = nearest (k, v); at >= 0)
        {
            const auto& it = h[(size_t) at];
            if (it.id != kn.picked || cueId != it.id)
            {
                kn.picked = it.id;
                cueScopeFor = it.id; cueScope = scopeOf (it.lanes);
                cueNotesWish = recallNotes || it.notes ? 1 : 0;        // (as its press would take them: pressSnapKnob)
                cueLacked = cue (it.id) ? 0 : it.id;       // (no sound kept with it: the screen says so)
                if (onChange) onChange();
            }
        }
    }
    else
    {
        // CUE out: the set itself, from where it is toward the next snapshot the way the knob is going
        const auto on = kn.picked;
        if (v != was) kn.going = v > was ? 1 : -1;
        sweepKnob (k, was, v);
        engine.set (cx::kSnap + k, v);
        applyMorph (glideBeats());
        if (kn.picked != on && onChange) onChange();        // (it has come to one, or left it: the panel names the one it stands on)
        if (kn.picked != on && kn.picked != 0) logAction ("knob", kn.picked, { { "knob", k + 1 }, { "how", "turn" } });
    }
    touch();
}

bool Session::reaches (const Engine& e, int ctl) { return ctl >= 0 && ctl < kMorphEnd && e.control (ctl).morph; }

bool Session::knobLive (int k) const
{
    if (k < 0 || k >= 16 || isCorner (k)) return false;
    const auto& kn = knobs[(size_t) k];
    if (! kn.links.empty() || k == learning) return true;
    if (! isStack (k)) return false;
    for (auto id : kn.snaps) if (findSnap (id) != nullptr) return true;           // (a stack of one is something to turn toward, and to press)
    return false;
}

Session::Scope Session::scopeOf (juce::uint32 m)
{
    Scope sc;
    for (int l = 0; l < kLanes; ++l) sc.lane[(size_t) l] = ((m >> l) & 1u) != 0;
    sc.returns = ((m >> kLanes) & 1u) != 0; sc.bus = ((m >> (kLanes + 1)) & 1u) != 0; sc.master = ((m >> (kLanes + 2)) & 1u) != 0;
    return sc;
}

juce::uint32 Session::lanesOf (const Scope& sc)
{
    juce::uint32 m = 0;
    for (int l = 0; l < kLanes; ++l) if (sc.lane[(size_t) l]) m |= 1u << l;
    if (sc.returns) m |= 1u << kLanes;
    if (sc.bus) m |= 1u << (kLanes + 1);
    if (sc.master) m |= 1u << (kLanes + 2);
    return m;
}

// A snapshot's keys as they are kept, from what a caller gives: its parts of the rig, and N if it was given lit (a
// figure that says nothing: every part, N out).
static juce::uint32 keysOf (juce::uint32 m) { return m == MacroKnob::kAll ? Session::kAllLanes : (m & Session::kAllKeys); }

std::vector<Session::Held> Session::held (int k) const
{
    std::vector<Held> h;
    if (k < 0 || k >= 16) return h;
    const auto& kn = knobs[(size_t) k];
    for (size_t i = 0; i < kn.snaps.size(); ++i)
        if (const auto* sn = findSnap (kn.snaps[i])) h.push_back ({ sn, kn.snaps[i], kn.keysAt (i) & kAllLanes, kn.notesAt (i), kn.blendAt (i) });
    return h;
}

int Session::nearest (int k, float v) const
{
    int n = 0;
    if (k >= 0 && k < 16) for (auto id : knobs[(size_t) k].snaps) if (findSnap (id) != nullptr) ++n;
    if (n == 0) return -1;
    return n == 1 ? 0 : juce::jlimit (0, n - 1, (int) std::lround (v * (float) (n - 1)));
}

juce::int64 Session::pickAt (int k) const
{
    const auto h = held (k);
    const int at = nearest (k, engine.target (cx::kSnap + k));
    return at >= 0 ? h[(size_t) at].id : 0;
}

void Session::setStackCue (bool on)
{
    if (on == stackCue) return;
    stackCue = on;
    for (int k = kMacroKnobs; k < 16; ++k)
    {
        if (! isStack (k)) continue;
        auto& kn = knobs[(size_t) k];
        // lit: each stack knob remembers where it stood (MOTION swings about that while the knob is used to choose). Out: the
        // knob stays where it was left. Its turns go on from there, from wherever the set is, so nothing jumps.
        if (on) kn.sweptAt = engine.target (cx::kSnap + k);
        kn.picked = 0;
    }
    if (! on) cue (0);
    lawsSig = 0;
    motionLaws();
    touch();
    if (onChange) onChange();
}

// Whether a snapshot of a stack, with those parts of the rig chosen for it, takes that control.
static bool scopeTakes (const Session::Scope& sc, int i)
{
    return i < cx::kRet ? sc.lane[(size_t) juce::jmin (kLanes - 1, i / cx::kPerLane)]
         : i < cx::kBus ? sc.returns : i < cx::kRetMS ? sc.bus : (i >= cx::kMaster && i < cx::kSnap) ? sc.master : true;
}

// CUE out, a stack knob turned from one place to another: the set goes toward the next snapshot in the
// direction of the turn by as much of the way as the knob went of its way to that snapshot's place.
// It is worked out from what is playing (the scene under the macro knobs), so there is nothing to
// jump from; at a snapshot's place the scene is that snapshot, for the parts of the rig it takes.
void Session::sweepKnob (int k, float from, float to)
{
    const auto h = held (k);
    const int n = (int) h.size();
    if (n == 0 || from == to) return;
    const float dir = to > from ? 1.0f : -1.0f;
    float at = from;
    for (int guard = 0; guard < 256 && dir * (to - at) > 1.0e-6f; ++guard)
    {
        int next = -1;
        if (dir > 0.0f) { for (int i = 0; i < n; ++i) if (placeOf (i, n) > at + 1.0e-6f) { next = i; break; } }
        else            { for (int i = n - 1; i >= 0; --i) if (placeOf (i, n) < at - 1.0e-6f) { next = i; break; } }
        if (next < 0) break;                            // (past its last: nothing further to go toward)
        const float span = std::abs (placeOf (next, n) - at), step = std::min (std::abs (to - at), span);
        const float f = span > 1.0e-6f ? step / span : 1.0f;
        const auto& snap = *h[(size_t) next].s;
        const auto sc = scopeOf (h[(size_t) next].lanes);
        for (int i = 0; i < kMorphEnd && i < (int) snap.values.size(); ++i)
        {
            if (! engine.control (i).morph || snap.values[(size_t) i] < 0.0f) continue;
            // (toward where the snapshot has it as it sounds: on its place the knob is there, whatever the macro knobs add)
            if (scopeTakes (sc, i)) base[(size_t) i] += (snap.values[(size_t) i] - offset[(size_t) i] - base[(size_t) i]) * f;
        }
        {
            // the effects in the racks go with it (sweepSlots). How far the knob is between the place behind it and the one ahead:
            const float ahead = placeOf (next, n), behind = (next - (int) dir >= 0 && next - (int) dir < n) ? placeOf (next - (int) dir, n) : (dir > 0.0f ? 0.0f : 1.0f);
            const float whole = std::abs (ahead - behind);
            const float g0 = whole > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, std::abs (at - behind) / whole) : 1.0f;
            const float g1 = whole > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, std::abs (at + dir * step - behind) / whole) : 1.0f;
            sweepSlots (h[(size_t) next], f, g0, g1);
            // and the lanes' own instruments, toward one whose M is lit (or back from one, to where the knob came from)
            const int bi = next - (int) dir;
            sweepInstruments (h[(size_t) next], bi >= 0 && bi < n ? h[(size_t) bi].id : 0, g1, step >= span);
        }
        at += dir * step;
        knobs[(size_t) k].picked = step >= span ? h[(size_t) next].id : 0;
        // come to one whose N is lit (or through it, on the way to the next): its notes, into the lanes it takes. The sound
        // came with the turn; what cannot travel comes with the arrival, as a part's notes come with its bar line.
        if (step >= span && h[(size_t) next].notes) bringNotes (snap, sc);
    }
    stacksLeft (k);
    baseId = 0;
}

static bool setupDiffers (const Engine::SetupState& a, const Engine::SetupState& b);
static Engine::SetupState withMotion (const Engine& engine, Engine::Target t, Engine::SetupState st);

bool Session::instrumentDiffers (const Snapshot& s, int l) const
{
    if (s.hasFiles && s.laneFiles[(size_t) l].isNotEmpty() && s.laneFiles[(size_t) l] != engine.laneFile (l)) return true;
    if (s.hasSetup && setupDiffers (engine.getSetup ({ 0, l, 0 }), withMotion (engine, { 0, l, 0 }, s.setup[(size_t) l]))) return true;
    for (int k = 0; k < kLaneKnobs; ++k)
    {
        const int i = cx::lane (l, k);
        const auto& c = engine.control (i);
        const float v = i < (int) s.values.size() ? s.values[(size_t) i] : -1.0f;
        // (a menu or a switch: what the turn itself does not carry)
        if (v >= 0.0f && c.used && ! c.glides && ! c.morph && std::abs (v - engine.target (i)) > 1.0e-4f) return true;
    }
    return false;
}

void Session::bringInstrument (const Snapshot& s, int l, std::vector<std::pair<int, float>>* was)
{
    for (int k = 0; k < kLaneKnobs; ++k)
    {
        const int i = cx::lane (l, k);
        const auto& c = engine.control (i);
        const float v = i < (int) s.values.size() ? s.values[(size_t) i] : -1.0f;
        if (v < 0.0f || ! c.used || c.glides || c.morph || std::abs (v - engine.target (i)) <= 1.0e-4f) continue;
        if (was != nullptr) was->push_back ({ i, engine.target (i) });
        base[(size_t) i] = v;
        engine.set (i, v, 0.0);
    }
    if (s.hasSetup) engine.applySetup ({ 0, l, 0 }, withMotion (engine, { 0, l, 0 }, s.setup[(size_t) l]));
    if (s.hasFiles && s.laneFiles[(size_t) l].isNotEmpty() && s.laneFiles[(size_t) l] != engine.laneFile (l))
        engine.loadFile (l, juce::File (s.laneFiles[(size_t) l]));
}

// A stack knob gone part of the way toward a snapshot (g1 of the way from the one behind it; arrived: at its place). Each
// lane that snapshot takes:
//   - held between this pair already: as much of the one that arrived as the knob has come (going on), or as much less
//     as it has gone back; at either end the lane settles on that one and the other is let go;
//   - not held, the snapshot's M lit, and its instrument another than the lane has: the lane is held between the one
//     playing and a copy made into the snapshot's, from here on.
// With M out nothing is done: the lane keeps the instrument it has, as it always did.
void Session::sweepInstruments (const Held& toward, juce::int64 behind, float g1, bool arrived)
{
    const auto sc = scopeOf (toward.lanes);
    for (int l = 0; l < kLanes; ++l)
    {
        auto& lh = laneHold[(size_t) l];
        if (! engine.holding (l)) lh = {};
        if (! sc.lane[(size_t) l]) continue;
        if (engine.holdEnding (l)) continue;                    // (settling: a fiftieth of a second, and it is one instrument again)
        bool on = false;                                        // going on toward the one that arrived (false: back to the one that was playing)
        if (engine.holding (l))
        {
            if (lh.ahead == toward.id) on = true;
            else if (lh.behind != toward.id || toward.id == 0) { engine.endHold (l, true); lh = {}; continue; }     // (the stack has changed under it: what arrived stays)
        }
        else
        {
            if (! toward.blend || toward.s == nullptr || g1 <= 0.0f || ! instrumentDiffers (*toward.s, l)) continue;
            LaneHold made; made.behind = behind; made.ahead = toward.id;
            const bool held = engine.holdVoice (l);
            bringInstrument (*toward.s, l, &made.was);
            if (! held) continue;                               // (no audio running: the lane simply has the snapshot's now)
            lh = made; on = true;
        }
        engine.blendVoice (l, on ? g1 : 1.0f - g1);
        if (! arrived) continue;
        if (! on)
        {
            // back where it came from: its menus and switches as they stood, and the lane to the one that was playing
            for (auto& w : lh.was) { base[(size_t) w.first] = w.second; engine.set (w.first, w.second, 0.0); }
        }
        engine.endHold (l, on);
        lh = {};
    }
}

void Session::settleHolds()
{
    for (int l = 0; l < kLanes; ++l)
    {
        if (engine.holding (l)) engine.endHold (l, true);
        laneHold[(size_t) l] = {};
    }
}

void Session::bringNotes (const Snapshot& s, const Scope& sc)
{
    const double glide = glideBeats();
    for (int l = 0; l < kLanes; ++l)
    {
        if (! sc.lane[(size_t) l]) continue;
        // (its generator's four knobs are its notes' and not its sound's: a recall without notes leaves them, this brings them)
        for (int c = cx::gen (l, 0); c < cx::gen (l, 0) + 4 && c < (int) s.values.size(); ++c)
        {
            const float v = s.values[(size_t) c];
            if (v < 0.0f) continue;
            const bool morph = engine.control (c).morph && c < kMorphEnd;
            base[(size_t) c] = morph ? v - offset[(size_t) c] : v;
            if (! morph) engine.set (c, v, 0.0);
        }
        engine.setPattern (l, played (s, l), true);         // (on the next bar line while the clock runs)
        genType[(size_t) l] = s.gen[(size_t) l];
        if (s.hasPlayed) engine.set (cx::lane (l, cx::kSeqMute), ! s.heardOn[(size_t) l] && s.seqMuted[(size_t) l] ? 1.0f : 0.0f, glide);
    }
    selStep = -1;
}

Session::StackWay Session::stackWay (int k, float v) const
{
    StackWay w;
    if (! isStack (k)) return w;
    const auto h = held (k);
    const int n = (int) h.size();
    if (n == 0) return w;
    v = juce::jlimit (0.0f, 1.0f, v);
    const float click = 0.6f / 127.0f;                      // (as a press takes "on its place": within a click of the E16's knob)
    w.at = juce::roundToInt (v * 127.0f); w.count = n;
    if (n == 1) { w.to = 0; w.part = v; if (v >= 1.0f - click) w.on = 0; }          // (one snapshot stands at the top of the turn)
    else
    {
        const bool down = knobs[(size_t) k].going < 0;
        const float x = v * (float) (n - 1);
        const int lo = juce::jlimit (0, n - 2, (int) std::floor (x)), near = juce::jlimit (0, n - 1, (int) std::lround (x));
        const float t = juce::jlimit (0.0f, 1.0f, x - (float) lo);
        w.from = down ? lo + 1 : lo; w.to = down ? lo : lo + 1; w.part = down ? 1.0f - t : t;
        if (std::abs (v - placeOf (near, n)) <= click) w.on = near;
    }
    if (w.from >= 0) w.fromName = h[(size_t) w.from].s->name;
    w.toName = h[(size_t) (w.on >= 0 ? w.on : w.to)].s->name;
    w.notes = h[(size_t) (w.on >= 0 ? w.on : w.to)].notes;
    return w;
}

// The set has gone somewhere by another way (another stack knob, a recall): no other stack knob stands on a snapshot
// of its own any more. (With CUE lit a knob's snapshot is the one it has chosen in the headphones: left as it is.)
void Session::stacksLeft (int but)
{
    if (stackCue) return;
    for (int k = kMacroKnobs; k < 16; ++k) if (k != but) knobs[(size_t) k].picked = 0;
}

void Session::applyMorph (double glide)
{
    std::fill (offset.begin(), offset.end(), 0.0f);
    for (int k = 0; k < 16; ++k)
    {
        const auto& kn = knobs[(size_t) k];
        const float v = engine.target (cx::kSnap + k);
        if (v <= 0.0f || isStack (k)) continue;     // (a stack knob moves the scene itself as it is turned: sweepKnob. It adds nothing on top.)
        // a macro knob: the knobs it is tied to, each its own share of the turn
        for (const auto& l : kn.links) if (reaches (engine, l.ctl)) offset[(size_t) l.ctl] += kn.depth * l.amount * v;
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
        mix ((juce::uint64) k + 1); mixF (kn.depth); mix (stackCue ? 2u : 1u); if (stackCue) mixF (kn.sweptAt);
        for (auto m : kn.lanes) mix (m);
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
        // a stack knob's law is its own path: at each of its snapshots' places, how far that snapshot has a control from the
        // first one that speaks of it. (More snapshots than a law has places: the path is read at that many even places.)
        const auto hs = isStack (k) ? held (k) : std::vector<Held>();
        const int n = (int) hs.size();
        law.nodes = n >= 2 ? juce::jmin (n, Engine::KnobLaw::kNodes) : 0;
        law.centre = isStack (k) && stackCue ? kn.sweptAt : -1.0f;
        for (int i = 0; i < kMorphEnd; ++i)
        {
            if (! engine.control (i).morph) continue;
            Engine::KnobLaw::Row r;
            r.ctl = i;
            if (! isStack (k)) for (const auto& l : kn.links) if (l.ctl == i) r.link += kn.depth * l.amount;
            bool moves = r.link != 0.0f;
            const bool laneCtl = i < cx::kRet, retCtl = i >= cx::kRet && i < cx::kBus, busCtl = i >= cx::kBus && i < cx::kRetMS, masCtl = i >= cx::kMaster && i < cx::kSnap;
            auto speaks = [&] (const Held& it)
            {
                if (i >= (int) it.s->values.size() || it.s->values[(size_t) i] < 0.0f) return false;
                const auto sc = scopeOf (it.lanes);
                return laneCtl ? sc.lane[(size_t) juce::jmin (kLanes - 1, i / cx::kPerLane)] : retCtl ? sc.returns : busCtl ? sc.bus : masCtl ? sc.master : true;
            };
            float first = 0.0f; bool got = false;
            for (int j = 0; j < law.nodes; ++j)
            {
                // (the snapshot at this place of the law: the stack's own, or the nearest of them where there are more than places)
                const auto& it = hs[(size_t) (law.nodes == n ? j : (int) std::lround ((float) j / (float) (law.nodes - 1) * (float) (n - 1)))];
                if (! speaks (it)) { r.node[(size_t) j] = Engine::KnobLaw::kNone; continue; }
                const float val = it.s->values[(size_t) i];
                if (! got) { first = val; got = true; }
                r.node[(size_t) j] = val - first;
                moves = moves || r.node[(size_t) j] != 0.0f;
            }
            if (moves) law.rows.push_back (r);
        }
    }
    engine.setKnobLaws (std::move (laws));
}

void Session::pressSnapKnob (int k)
{
    if (k < 0 || k >= 16 || k == learning || isCorner (k)) return;
    if (k == kCueKnob) { setStackCue (! stackCue); return; }
    auto& kn = knobs[(size_t) k];
    if (! isStack (k))
    {
        // A macro knob: there is no snapshot to land on, so it lands on the nearer end of its own
        // turn. Past half way what it would push at full is where those knobs stand from now on;
        // short of it they go back. Either way the knob goes home.
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
    // A stack knob: the set goes to what the knob's place says, over the GLIDE time, and the knob stays where it was left.
    // On a snapshot's place (to within a click of the E16): that snapshot, whole, for the parts of the rig it takes.
    // Between two: every knob of the rig as far between where the two have it as the place is between theirs (a part
    // only one of the two takes: between where that one has it and where it stands now). What is not a knob (the
    // notes, the inserts in the rack, the samples) is a snapshot's own, and comes only with a press on its place.
    const auto h = held (k);
    const int n = (int) h.size();
    if (n == 0) return;
    const float v = engine.target (cx::kSnap + k);
    const int at = nearest (k, v);
    kn.sweptAt = v;                                         // (where the set now is, along this knob)
    if (n == 1 || std::abs (v - placeOf (at, n)) <= 0.6f / 127.0f)
    {
        const auto it = h[(size_t) at];
        auto sc = scopeOf (it.lanes);
        sc.notes = recallNotes || it.notes;         // (the library's NOTES, as before; or its own N)
        logFrom = "knob"; logKnob = k;
        recall (it.id, sc);
        logFrom = nullptr; logKnob = -1;
        kn.picked = it.id;
    }
    else
    {
        const float p = v * (float) (n - 1);
        const int a = juce::jlimit (0, n - 2, (int) p);
        const float f = p - (float) a;
        const auto& A = h[(size_t) a];
        const auto& B = h[(size_t) a + 1];
        const auto sa = scopeOf (A.lanes), sb = scopeOf (B.lanes);
        for (int i = 0; i < kMorphEnd; ++i)
        {
            if (! engine.control (i).morph) continue;
            const bool ta = i < (int) A.s->values.size() && A.s->values[(size_t) i] >= 0.0f && scopeTakes (sa, i);
            const bool tb = i < (int) B.s->values.size() && B.s->values[(size_t) i] >= 0.0f && scopeTakes (sb, i);
            if (! ta && ! tb) continue;
            // (as it sounds: what the macro knobs add now is in "now", and is taken off the scene beneath again)
            const float off = offset[(size_t) i], now = base[(size_t) i] + off, va = ta ? A.s->values[(size_t) i] : now, vb = tb ? B.s->values[(size_t) i] : now;
            base[(size_t) i] = juce::jlimit (0.0f, 1.0f, va + (vb - va) * f) - off;
        }
        // the effects in the racks, that far between the two (slotBetween): a lane neither takes is left as it is
        for (int l = 0; l < kLanes; ++l)
        {
            if (! sa.lane[(size_t) l] && ! sb.lane[(size_t) l]) continue;
            for (int s = 0; s < kSlots; ++s)
                slotTo (l, s, slotBetween (slotOf (sa.lane[(size_t) l] ? A.s : nullptr, l, s), slotOf (sb.lane[(size_t) l] ? B.s : nullptr, l, s), f), recallGlide());
        }
        logAction ("between", A.id, { { "with", uidOf (B.id) }, { "f", (double) f }, { "knob", k + 1 } });
        stacksLeft (k);
        kn.picked = 0;                                      // (between two: it stands on neither)
        baseId = 0;
        if (learning >= 0) learnBase = base;
        applyMorph (recallGlide());
        touch();
        if (onChange) onChange();
    }
    if (stackCue) { cue (0); lawsSig = 0; motionLaws(); }
}

bool Session::stackAdd (int k, juce::int64 id, juce::uint32 lanes)
{
    if (! isStack (k) || findSnap (id) == nullptr) return false;
    auto& kn = knobs[(size_t) k];
    if ((int) kn.snaps.size() >= kStackMost) return false;
    // Where the knob stands goes on meaning the snapshot it stood on (the places move as the stack grows)
    const auto on = kn.picked;
    kn.add (id, keysOf (lanes));
    stackSettle (k, on);
    logAction ("add", id, { { "to", "knob" }, { "knob", k + 1 }, { "at", (int) kn.snaps.size() - 1 }, { "parts", (int) (keysOf (lanes) & kAllLanes) } });
    return true;
}

void Session::stackDrop (int k, int index)
{
    if (! isStack (k)) return;
    auto& kn = knobs[(size_t) k];
    if (index < 0 || index >= (int) kn.snaps.size()) return;
    logAction ("drop", kn.snaps[(size_t) index], { { "from", "knob" }, { "knob", k + 1 }, { "at", index } });
    const auto on = kn.picked == kn.snaps[(size_t) index] ? 0 : kn.picked;
    kn.dropAt ((size_t) index);
    kn.picked = on;
    stackSettle (k, on);
}

void Session::stackSwap (int k, int a, int b)
{
    if (! isStack (k)) return;
    auto& kn = knobs[(size_t) k];
    const auto on = kn.picked;
    kn.swapAt ((size_t) a, (size_t) b);
    stackSettle (k, on);
}

void Session::stackMove (int k, int from, int to)
{
    if (! isStack (k)) return;
    auto& kn = knobs[(size_t) k];
    const int n = (int) kn.snaps.size();
    if (from < 0 || from >= n) return;
    to = juce::jlimit (0, n - 1, to);
    if (to == from) return;
    const auto on = kn.picked;
    logAction ("move", kn.snaps[(size_t) from], { { "in", "knob" }, { "knob", k + 1 }, { "at", from }, { "to", to } });
    for (int i = from; i < to; ++i) kn.swapAt ((size_t) i, (size_t) i + 1);
    for (int i = from; i > to; --i) kn.swapAt ((size_t) i, (size_t) i - 1);
    stackSettle (k, on);
}

bool Session::stackInsert (int k, int at, juce::int64 id, juce::uint32 lanes)
{
    if (! isStack (k) || findSnap (id) == nullptr) return false;
    auto& kn = knobs[(size_t) k];
    if ((int) kn.snaps.size() >= kStackMost) return false;
    const auto on = kn.picked;
    kn.add (id, keysOf (lanes));
    const int last = (int) kn.snaps.size() - 1;
    for (int i = last; i > juce::jlimit (0, last, at); --i) kn.swapAt ((size_t) i, (size_t) i - 1);
    stackSettle (k, on);
    logAction ("add", id, { { "to", "knob" }, { "knob", k + 1 }, { "at", juce::jlimit (0, last, at) }, { "parts", (int) (keysOf (lanes) & kAllLanes) } });
    return true;
}

void Session::stackLanes (int k, int index, juce::uint32 lanes)
{
    if (! isStack (k)) return;
    knobs[(size_t) k].setLanes ((size_t) index, keysOf (lanes));
    lawsSig = 0; motionLaws();
    touch();
    if (onChange) onChange();
}

// The stack is not what it was: a knob standing on one of its snapshots goes to that one's new place (nothing is heard
// to change: a stack knob adds nothing by standing anywhere), and MOTION's law for it is made again.
void Session::stackSettle (int k, juce::int64 on)
{
    auto& kn = knobs[(size_t) k];
    const auto h = held (k);
    if (on != 0 && ! stackCue)
        for (int i = 0; i < (int) h.size(); ++i)
            if (h[(size_t) i].id == on) { engine.set (cx::kSnap + k, placeOf (i, (int) h.size())); break; }
    if (h.empty()) { kn.picked = 0; if (! stackCue) engine.set (cx::kSnap + k, 0.0f); }
    lawsSig = 0; motionLaws();
    touch();
    if (onChange) onChange();
}

void Session::knobClear (int k, bool told)
{
    if (k < 0 || k >= 16) return;
    if (learning == k) learnDrop();
    auto& kn = knobs[(size_t) k];
    if (const auto lib = kn.more.getWithDefault ("lib", "").toString(); lib.isNotEmpty() && told)
    {
        // (the module it was, in the web library: to be taken off the chassis there too)
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", lib); o->setProperty ("slot", k + 1);
        knobsUnseated.add (var (o));
    }
    kn.links.clear(); kn.linksKept.clear(); kn.clearSnaps(); kn.name = {}; kn.depth = 1.0f; kn.picked = 0; kn.sweptAt = 0.0f;
    kn.more.remove ("lib"); kn.more.remove ("libmark"); kn.more.remove ("reseat");
    engine.set (cx::kSnap + k, 0.0f);
    applyMorph (glideBeats());
    lawsSig = 0; motionLaws();
    touch();
    if (onChange) onChange();
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
        if (tied && ! isStack (k)) { out.push_back (i); continue; }
        if (! isStack (k) || ss.size() < 2) continue;
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
    if (k < 0 || k >= kMacroKnobs) return;          // (the four macro knobs are the ones that are taught)
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
juce::File Session::stemsFile (juce::int64 id) const { return stateFile().getSiblingFile ("clips").getChildFile (juce::String (id) + ".stems"); }
juce::File Session::losslessFile (juce::int64 id) const { return stateFile().getSiblingFile ("clips").getChildFile (juce::String (id) + ".lossless"); }
juce::String Session::stemKey (int index)
{
    if (index >= 0 && index < kLanes) return lanes()[(size_t) index].slug;
    return index == kLanes ? "dub" : index == kLanes + 1 ? "space" : index == kLanes + 2 ? "bus" : "transition";
}

// A stems file: a line that says what it is, a line of JSON (the rate, the bars, and for each stem
// its key and how many bytes it is), then the stems one after another, each an Ogg Vorbis file.
static const char* const kStemsMark = "SF0STEMS1\n";
bool Session::readStems (juce::int64 id, Stems& out, const Scope* only) const
{
    out = Stems();
    juce::MemoryBlock all;
    if (! stemsFile (id).loadFileAsData (all) || all.getSize() < 16) return false;
    const char* d = (const char*) all.getData();
    const size_t size = all.getSize(), markLen = std::strlen (kStemsMark);
    if (std::memcmp (d, kStemsMark, markLen) != 0) return false;
    size_t eol = markLen;
    while (eol < size && d[eol] != '\n') ++eol;
    if (eol >= size) return false;
    const auto head = juce::JSON::parse (juce::String::fromUTF8 (d + markLen, (int) (eol - markLen)));
    const auto* list = head["stems"].getArray();
    if (list == nullptr) return false;
    out.rate = (double) head["rate"]; out.bars = (int) head["bars"];
    auto wanted = [only] (const juce::String& key)
    {
        if (only == nullptr) return true;
        if (key == "dub" || key == "space") return only->returns;
        if (key == "bus") return only->bus;
        for (int l = 0; l < kLanes; ++l) if (key == lanes()[(size_t) l].slug) return only->lane[(size_t) l];
        return false;
    };
    size_t at = eol + 1;
    juce::OggVorbisAudioFormat ogg;
    for (const auto& e : *list)
    {
        const auto key = e["key"].toString();
        // (the length as the file says it, checked against what is left before it is used: not added to a place first)
        const juce::int64 said = (juce::int64) e["bytes"];
        if (said <= 0 || at > size || (juce::uint64) said > (juce::uint64) (size - at)) return false;
        const auto bytes = (size_t) said;
        if (wanted (key))
            if (std::unique_ptr<juce::AudioFormatReader> r { ogg.createReaderFor (new juce::MemoryInputStream (d + at, bytes, false), true) })
                if (r->lengthInSamples > 1 && r->lengthInSamples < 48000 * 120)
                {
                    juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
                    r->read (&b, 0, b.getNumSamples(), 0, true, true);
                    out.parts.push_back ({ key, std::move (b) });
                }
        at += bytes;
    }
    return true;
}

// A lossless file: a line that says what it is, a line of JSON (the rate, the bars, the bit depth, and for each part its
// key, how many bytes it is and the gain it was written at), then the parts one after another, each a FLAC file. The
// first is "mix", the output; the rest are the stems that sounded, by their keys.
static const char* const kLosslessMark = "SF0LOSSLESS1\n";
bool Session::readLossless (const juce::File& file, Lossless& out)
{
    out = Lossless();
    juce::MemoryBlock all;
    if (! file.loadFileAsData (all) || all.getSize() < 16) return false;
    const char* d = (const char*) all.getData();
    const size_t size = all.getSize(), markLen = std::strlen (kLosslessMark);
    if (std::memcmp (d, kLosslessMark, markLen) != 0) return false;
    size_t eol = markLen;
    while (eol < size && d[eol] != '\n') ++eol;
    if (eol >= size) return false;
    const var head = juce::JSON::parse (juce::String::fromUTF8 (d + markLen, (int) (eol - markLen)));
    auto* list = head["parts"].getArray();
    if (list == nullptr) return false;
    out.rate = (double) head["rate"]; out.bars = (int) head["bars"]; out.bits = (int) head["bits"];
    size_t at = eol + 1;
    juce::FlacAudioFormat flac;
    for (const auto& e : *list)
    {
        // (the length as the file says it, checked against what is left before it is used)
        const juce::int64 said = (juce::int64) e["bytes"];
        if (said <= 0 || at > size || (juce::uint64) said > (juce::uint64) (size - at)) return false;
        const auto bytes = (size_t) said;
        const float gain = e.hasProperty ("gain") ? (float) (double) e["gain"] : 1.0f;
        if (! (gain > 0.0f)) return false;
        std::unique_ptr<juce::AudioFormatReader> r { flac.createReaderFor (new juce::MemoryInputStream (d + at, bytes, false), true) };
        if (r == nullptr || r->lengthInSamples < 2 || r->lengthInSamples > 48000 * 240) return false;
        juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
        b.clear();
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        b.applyGain (1.0f / gain);
        out.parts.push_back ({ e["key"].toString(), std::move (b) });
        out.gains.push_back (gain);
        at += bytes;
    }
    return at == size && ! out.parts.empty();
}

// (written by the clips' own thread: see keepClip)
void Session::writeLossless (const juce::File& file, const juce::AudioBuffer<float>& mix, const std::vector<juce::AudioBuffer<float>>* stems, double rate, int bars)
{
    juce::FlacAudioFormat flac;
    juce::Array<var> list;
    juce::MemoryBlock body;
    auto add = [&] (const juce::String& key, const juce::AudioBuffer<float>& b)
    {
        const int n = b.getNumSamples();
        if (n < 2 || b.getNumChannels() < 2) return;
        const float peak = b.getMagnitude (0, n);
        if (key != "mix" && peak < 1.0e-4f) return;                     // (a lane that was silent is left out, as its stem is)
        // (24 bits hold what is within full scale. A stem that ran over it, as one may before the master, is written
        // quieter by a power of two, which loses nothing of a float, and says so)
        float gain = 1.0f;
        while (peak * gain > 0.999f && gain > 1.0e-6f) gain *= 0.5f;
        juce::MemoryBlock one;
        {
            auto out = std::make_unique<juce::MemoryOutputStream> (one, false);
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (out.get(), rate, 2, 24, {}, 4));
            if (w == nullptr) return;
            out.release();
            bool ok = false;
            if (gain == 1.0f) ok = w->writeFromAudioSampleBuffer (b, 0, n);
            else { juce::AudioBuffer<float> q; q.makeCopyOf (b); q.applyGain (gain); ok = w->writeFromAudioSampleBuffer (q, 0, n); }
            if (! ok) return;
        }
        if (one.getSize() < 8) return;
        auto* o = new juce::DynamicObject();
        o->setProperty ("key", key); o->setProperty ("bytes", (juce::int64) one.getSize()); o->setProperty ("gain", (double) gain);
        list.add (var (o));
        body.append (one.getData(), one.getSize());
    };
    add ("mix", mix);
    if (stems != nullptr) for (size_t k = 0; k < stems->size(); ++k) add (Session::stemKey ((int) k), (*stems)[k]);
    if (list.isEmpty()) { file.deleteFile(); return; }
    auto* h = new juce::DynamicObject();
    h->setProperty ("rate", rate); h->setProperty ("bars", bars); h->setProperty ("bits", 24); h->setProperty ("parts", list);
    const auto head = juce::JSON::toString (var (h), true);
    file.getParentDirectory().createDirectory();
    const auto part = file.getSiblingFile (file.getFileName() + ".part");
    part.deleteFile();
    bool ok = false;
    if (auto out = part.createOutputStream())
        ok = out->write (kLosslessMark, std::strlen (kLosslessMark)) && out->writeText (head, false, false, nullptr) && out->writeByte ('\n') && out->write (body.getData(), body.getSize());
    if (ok) part.moveFileTo (file); else part.deleteFile();
}

void Session::waitForClips() { while (clipWriter.getNumJobs() > 0) juce::Thread::sleep (2); }

// ------------------------------------------------------------------------------------ the log
juce::File Session::logDir() const { return stateFile().getSiblingFile ("log"); }
void Session::waitForLog() { while (logWriter.getNumJobs() > 0) juce::Thread::sleep (2); }
juce::String Session::uidOf (juce::int64 id) const { const auto* sn = id != 0 ? findSnap (id) : nullptr; return sn != nullptr ? sn->uid : juce::String(); }

// A line: its own name (k: when, the count since this run began, this run's mark: no two alike, with nothing kept
// between runs), when (t, ms), what was done (a), the chip it was done to (u, its uid; one the rig came with has its
// number here and its name too, "id" and "name"), and whatever else says how. The chip the set was last on is "base"
// where that matters.
void Session::logAction (const char* what, juce::int64 chip, const juce::NamedValueSet& more)
{
    if (logQuiet > 0 || printRig) return;
    const auto t = juce::Time::currentTimeMillis();
    if (logRun.isEmpty())
    {
        // (the first line of a run says which build is writing: what a line means is that build's to say)
        logRun = juce::String::toHexString (juce::Random::getSystemRandom().nextInt (0x10000)).paddedLeft ('0', 4);
        logAction ("open", 0, { { "app", madeBy() }, { "library", libraryId } });
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("k", juce::String::toHexString (t) + "-" + juce::String::toHexString (++logCount).paddedLeft ('0', 4) + "-" + logRun);
    o->setProperty ("t", t);
    o->setProperty ("a", what);
    if (chip != 0)
    {
        const auto u = uidOf (chip);
        if (u.isNotEmpty()) o->setProperty ("u", u);
        // (one the rig came with is never sent to the library, so its name there would find nothing: its number and name here say which it is)
        if (u.isEmpty() || chip < 1000000) { o->setProperty ("id", chip); if (const auto* sn = findSnap (chip)) o->setProperty ("name", sn->name); }
    }
    for (const auto& m : more) o->setProperty (m.name, m.value);
    const auto line = juce::JSON::toString (var (o), true);
    logTail.add (line);
    while (logTail.size() > 400) logTail.remove (0);
    if (! useDisk) return;
    const auto file = logDir().getChildFile (juce::Time (t).formatted ("%Y-%m") + ".jsonl");
    logWriter.addJob ([file, line] { file.getParentDirectory().createDirectory(); file.appendText (line + "\n", false, false, "\n"); });
}

// Written by a thread of its own: Ogg Vorbis, a hundred-odd kilobytes for four bars.
void Session::keepClip (juce::int64 id, const Engine::Heard& heard)
{
    auto audio = std::make_shared<juce::AudioBuffer<float>> (heard.audio);
    const auto file = clipFile (id);
    const double rate = heard.sampleRate;
    // its lanes, beside it: each stem that sounded, in one file (see readStems)
    std::shared_ptr<std::vector<juce::AudioBuffer<float>>> stems;
    if (! heard.stems.empty())
    {
        stems = std::make_shared<std::vector<juce::AudioBuffer<float>>> (heard.stems);
        const auto sfile = stemsFile (id);
        const int bars = heard.bars;
        clipWriter.addJob ([stems, sfile, rate, bars]
        {
            juce::OggVorbisAudioFormat ogg;
            juce::Array<juce::var> list;
            juce::MemoryBlock body;
            for (size_t k = 0; k < stems->size(); ++k)
            {
                const auto& b = (*stems)[k];
                if (b.getNumSamples() < 2 || b.getMagnitude (0, b.getNumSamples()) < 1.0e-4f) continue;         // (a lane that was silent is left out)
                juce::MemoryBlock one;
                {
                    auto out = std::make_unique<juce::MemoryOutputStream> (one, false);
                    std::unique_ptr<juce::AudioFormatWriter> w (ogg.createWriterFor (out.get(), rate, 2, 16, {}, 5));
                    if (w == nullptr) continue;
                    out.release();
                    if (! w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) continue;
                }
                auto* o = new juce::DynamicObject();
                o->setProperty ("key", stemKey ((int) k)); o->setProperty ("bytes", (juce::int64) one.getSize());
                list.add (juce::var (o));
                body.append (one.getData(), one.getSize());
            }
            if (list.isEmpty()) { sfile.deleteFile(); return; }
            auto* h = new juce::DynamicObject();
            h->setProperty ("rate", rate); h->setProperty ("bars", bars); h->setProperty ("stems", list);
            const auto head = juce::JSON::toString (juce::var (h), true);
            sfile.getParentDirectory().createDirectory();
            const auto part = sfile.getSiblingFile (sfile.getFileName() + ".part");
            part.deleteFile();
            bool ok = false;
            if (auto out = part.createOutputStream())
                ok = out->write (kStemsMark, std::strlen (kStemsMark)) && out->writeText (head, false, false, nullptr) && out->writeByte ('\n') && out->write (body.getData(), body.getSize());
            if (ok) part.moveFileTo (sfile); else part.deleteFile();
        });
    }
    else { const auto sfile = stemsFile (id); clipWriter.addJob ([sfile] { sfile.deleteFile(); }); }       // (no lanes for these bars: none from other bars are left beside them)
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
    // the same bars without loss, last (what the instrument plays is on the disk first): for the library to be studied from
    if (keepLossless)
    {
        const auto lfile = losslessFile (id);
        const int bars = heard.bars;
        clipWriter.addJob ([audio, stems, lfile, rate, bars] { writeLossless (lfile, *audio, stems.get(), rate, bars); });
    }
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

// (the log: a chip put in the headphones that was not the one there, with the parts it is heard with)
bool Session::cue (juce::int64 id)
{
    const auto was = cueId;
    const bool ok = cueDo (id);
    if (ok && id != 0 && cueId == id && was != id)
        logAction ("hear", id, { { "parts", (int) (id == cueScopeFor ? lanesOf (cueScope) : kAllLanes) }, { "base", uidOf (baseId) } });
    return ok;
}

bool Session::cueDo (juce::int64 id)
{
    const auto now = juce::Time::getMillisecondCounter();
    auto retire = [this, now] { for (auto& k : cueKept) if (k.second == 0) k.second = now == 0 ? 1u : now; };
    pruneCues();
    const Snapshot* s = id != 0 ? findSnap (id) : nullptr;
    // A second rig, if there is one: it is made what this rig is now and given the recall this cue stands for (the
    // parts lit, the notes that recall would bring), and that is what plays. No clip is played beside it.
    if (s != nullptr && cueRigPlay)
    {
        const juce::uint32 lanes = id == cueScopeFor ? lanesOf (cueScope) : kAllLanes;
        const bool notes = id == cueScopeFor && cueNotesWish >= 0 ? cueNotesWish > 0 : recallNotes;
        if (cueRigPlay (id, lanes, notes))
        {
            retire();
            engine.setCue (nullptr);
            const auto was = cueId; const bool wasRig = cueByRig;
            cueId = id; cueByRig = true;
            if ((cueId != was || ! wasRig) && onChange) onChange();
            return true;
        }
    }
    if (cueByRig) { cueByRig = false; if (cueRigStop) cueRigStop(); }
    std::unique_ptr<Engine::CueClip> clip;
    // narrowed to some of its lanes: those lanes' stems, together (with none of them kept, the whole clip, as before)
    bool narrowed = false;
    if (s != nullptr && id == cueScopeFor)
    {
        bool whole = cueScope.returns && cueScope.bus;
        for (bool b : cueScope.lane) whole = whole && b;
        if (! whole)
        {
            if (! stemsFile (id).existsAsFile() && ! clipFile (id).existsAsFile()) waitForClips();
            Stems st;
            if (readStems (id, st, &cueScope))
            {
                narrowed = true;                                        // (nothing of it in this scope is silence, which is the answer)
                int len = 0;
                for (auto& p : st.parts) len = std::max (len, p.second.getNumSamples());
                clip = std::make_unique<Engine::CueClip>();
                if (len > 1)
                {
                    clip->audio.setSize (2, len); clip->audio.clear();
                    for (auto& p : st.parts) for (int ch = 0; ch < 2; ++ch) clip->audio.addFrom (ch, 0, p.second, ch, 0, p.second.getNumSamples());
                    clip->bars = st.bars > 0 ? st.bars : (s->profile.has ? s->profile.bars : 0);
                    clip->rate = st.rate;
                }
                else { clip->audio.setSize (2, 4800); clip->audio.clear(); }          // (it is still the cue, so that a key lit again brings its lane back)
            }
        }
    }
    if (s != nullptr && ! narrowed)
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

const Session::Scope* Session::cueNarrowed (juce::int64 id) const { return id != 0 && cueScopeFor == id ? &cueScope : nullptr; }

void Session::cueWith (juce::int64 id, const Scope& scope, int notes)
{
    cueScopeFor = id; cueScope = scope; cueNotesWish = notes;
    if (id != 0 && cueId == id) cue (id);                           // (it is playing: it is made again from what the scope takes now)
}

// ---- a cue that is the rig
juce::var Session::cueState (juce::int64 target)
{
    const var whole = stateVar();
    auto* o = new juce::DynamicObject();
    if (auto* w = whole.getDynamicObject())
        for (auto& p : w->getProperties()) if (p.name != juce::Identifier ("library")) o->setProperty (p.name, p.value);
    // the rig as it stands, with what every lane is playing that its steps do not say (as a snapshot taken now would hold it)
    Snapshot nowIs = capture();
    pullHeard();
    for (int l = 0; l < kLanes; ++l)
    {
        nowIs.heardOn[(size_t) l] = heardPattern (l, nowIs.heard[(size_t) l], &nowIs.heardNotes[(size_t) l]);
        nowIs.seqMuted[(size_t) l] = engine.target (cx::lane (l, cx::kSeqMute)) > 0.5f;
    }
    nowIs.hasPlayed = true;
    nowIs.id = kCueNowId; nowIs.name = "NOW"; nowIs.kept = var(); nowIs.uid.clear();
    juce::Array<var> lib;
    lib.add (written (nowIs));
    if (const auto* t = findSnap (target)) lib.add (written (*t));
    o->setProperty ("library", lib);
    return var (o);
}

void Session::cueRigTake (const juce::var& state, juce::int64 target, juce::uint32 lanes, bool notes)
{
    if (! state.isObject()) return;
    takeState (state);
    // (nothing plays into this rig from outside: each lane's sequencer is given what the first rig's lane was playing)
    if (findSnap (kCueNowId) != nullptr) recallOver (kCueNowId, 0.0, true, kAllLanes);
    if (target != 0 && findSnap (target) != nullptr) recallOver (target, 0.0, notes, lanes);
}

juce::int64 Session::take()
{
    const auto logBase = uidOf (baseId);        // (the log: what the set was last on, before this is)
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
    ++takes; takenName = s.name;
    // Its sound, with nothing asked: the bars the rig has held this state through, profiled and kept
    // as a clip. Held for four bars already: those. Something has just moved, or is still on its
    // way: it waits for them (finishTakes), so the clip is the snapshot and not the road to it.
    const auto id = s.id;
    const auto stamp = engine.stateStamp();
    Engine::Heard heard;
    const bool heldNow = engine.heldHeard (heard, stamp, 4, true) && heard.bars >= 4;
    library.push_back (s);
    sign();
    if (heldNow) settle (id, heard, true);
    else if (engine.isRunning())
    {
        Waiting w;
        w.id = id; w.stamp = stamp;
        w.hasBefore = engine.lastHeard (w.before, 4, true);
        waiting.push_back (std::move (w));
        startTimer (400);
    }
    else if (engine.lastHeard (heard, 4, true)) settle (id, heard, false);     // the clock is stopped: what was last heard, if anything
    touch();
    if (onLibrary) onLibrary();
    logAction ("take", id, { { "base", logBase }, { "running", engine.isRunning() }, { "bpm", (double) engine.tempo() } });
    return id;
}

// ---- a sample as it was recorded
static const Engine::SetupParam* setupOf (const std::vector<Engine::SetupParam>& ps, const char* id)
{
    for (auto& sp : ps) if (sp.id == id) return &sp;
    return nullptr;
}

bool Session::playsSample (int lane) const
{
    if (lane < 0 || lane >= kLanes || engine.laneFile (lane).isEmpty()) return false;
    const auto ps = engine.setupParams ({ 0, lane, 0 });
    const auto* model = setupOf (ps, "model");
    // (RUMBLE is driven by the kick and shapes whatever it is given: a sample there is its material, not a recording to play back)
    return model != nullptr && model->text.equalsIgnoreCase ("SAMPLE") && setupOf (ps, "source") == nullptr;
}

Session::SampleSays Session::sampleSays (int lane) const
{
    SampleSays out;
    if (lane < 0 || lane >= kLanes || engine.laneFile (lane).isEmpty()) return out;
    // (the sample as the lane's module holds it: read once, and shared)
    // (as much of it as the lane's module reads: the drums a minute, the others five)
    const juce::String module (lanes()[(size_t) lane].module);
    const double most = module == "KICK" || module == "PERC" || module == "RUMBLE" ? 60.0 : 300.0;
    if (const auto smp = sf::loadSampleFile (juce::File (engine.laneFile (lane)), engine.sampleRate(), most))
    {
        out.has = true;
        out.pitched = smp->rootHz > 0.0f;
        out.rootNote = out.pitched ? (int) std::lround (smp->rootNote) : 60;
        out.root = juce::MidiMessage::getMidiNoteName (out.rootNote, true, true, 3);
        out.peakDb = juce::Decibels::gainToDecibels (smp->chanPeak, -100.0f);
    }
    return out;
}

juce::String Session::resampleInto (int lane)
{
    if (lane < 0 || lane >= kLanes) return {};
    // the last four bars the rig played, from a bar line, as they left the master (the engine's own tape of them, which
    // is what a snapshot's bars are taken from): or its last seconds, where no clock was running
    Engine::Heard h;
    if (! engine.lastHeard (h, 4) || h.audio.getNumSamples() < 2 || h.sampleRate <= 0.0) return "NOTHING HAS PLAYED YET TO RESAMPLE";
    juce::AudioBuffer<float> bars;
    bars.makeCopyOf (h.audio);
    if (bars.getNumChannels() < 2) return "NOTHING HAS PLAYED YET TO RESAMPLE";
    // (a file holds no more than full scale: bars that ran over it are written that much quieter)
    if (const float peak = bars.getMagnitude (0, bars.getNumSamples()); peak > 0.98f) bars.applyGain (0.98f / peak);
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SF-000 resample");
    dir.createDirectory();
    const auto file = dir.getChildFile ("RESAMPLE " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        auto out = file.createOutputStream();
        if (out == nullptr) return "COULD NOT WRITE THE RESAMPLE";
        std::unique_ptr<juce::AudioFormatWriter> w { wav.createWriterFor (out.get(), h.sampleRate, 2, 24, {}, 0) };
        if (w == nullptr) return "COULD NOT WRITE THE RESAMPLE";
        out.release();
        w->writeFromAudioSampleBuffer (bars, 0, bars.getNumSamples());
    }
    const juce::String kept = keepSample (file.getFullPathName());
    if (! engine.loadFile (lane, juce::File (kept))) return "COULD NOT READ THE RESAMPLE BACK";
    if (kept != file.getFullPathName()) file.deleteFile();          // (the library has its copy)
    touch();
    return h.bars > 0 ? "THE LAST " + juce::String (h.bars) + (h.bars == 1 ? " BAR IS" : " BARS ARE") + " THIS LANE'S SAMPLE" : juce::String ("THE LAST SECONDS ARE THIS LANE'S SAMPLE");
}

juce::String Session::samplePlain (int lane)
{
    if (! playsSample (lane)) return {};
    const Engine::Target t { 0, lane, 0 };
    const auto ps = engine.setupParams (t);
    const juce::String slug (lanes()[(size_t) lane].slug);
    // one of the module's own values, by the lane's knob that drives it if one does, else in CONFIG
    auto put = [&] (const char* id, float v)
    {
        for (int k = 0; k < kLaneKnobs; ++k)
            if (juce::String (engine.laneKnob (lane, k).param) == id) { userSet (cx::lane (lane, k), v); return true; }
        if (setupOf (ps, id) == nullptr) return false;
        engine.setSetup (t, id, v);
        return true;
    };
    auto has = [&] (const char* id)
    {
        if (setupOf (ps, id) != nullptr) return true;
        for (int k = 0; k < kLaneKnobs; ++k) if (juce::String (engine.laneKnob (lane, k).param) == id) return true;
        return false;
    };
    auto choose = [&] (const char* id, const char* name)
    {
        if (const auto* sp = setupOf (ps, id)) if (const int at = sp->choices.indexOf (name); at >= 0) engine.setSetup (t, id, (float) at / (float) std::max (1, sp->choices.size() - 1));
    };
    // What shapes a sample, each where it does nothing (measured, lane by lane, against the file: --samplecheck).
    const bool synth = has ("cutoff");              // BASS, LEAD, CHORD, PAD: a filter, an envelope, a layer; the note is the pitch
    put ("tune", 0.5f);
    put ("drive", 0.0f); put ("filter", 0.5f); put ("density", 0.0f);
    put ("layer", 0.0f); put ("split", 0.5f);       // (the crossover takes the model's lows out even with LAYER at nought)
    put ("gate", 1.0f); put ("pocket_depth", 0.0f);
    if (synth)
    {
        put ("cutoff", 1.0f); put ("res", 0.0f); put ("env_amt", 0.0f); put ("glide", 0.0f); put ("attack", 0.0f);
        put ("acid", 0.0f); put ("warp", 0.0f); put ("drift", 0.0f);
        choose ("chord", "NONE");
        choose ("smp_play", "THROUGH");             // (a note plays it to its end, however short the step)
        // (KNOBS on: every knob of the lane acts on the sample, and each is put where it does nothing)
        put ("smp_env", 1.0f); put ("decay", 1.0f); put ("detune", 0.0f); put ("space", 0.0f); put ("color", 0.5f);
    }
    else { put ("decay", 1.0f); put ("click", 0.0f); put ("key_track", 0.0f); put ("echo", 0.0f); put ("tone", 0.5f); put ("signal", 1.0f); }      // (the drums: DECAY is how much of it plays; TONE tilts a sample about its centre)
    choose ("slice_mode", "WHOLE");                 // (VOICE: any note is the whole phrase, at its own pitch)
    for (int e = 0; e < 3; ++e) userSet (cx::lane (lane, cx::kEqLow + e), 0.5f);
    // Its pitch. A drum and a phrase play at their own whatever the note; the others at the note's, and at their own on their root.
    const auto says = sampleSays (lane);
    juce::String told = "PLAIN: THE SAMPLE AS IT WAS RECORDED";
    if (synth && says.has)
    {
        // TUNE, so that the lane's first lit step is that root (where TUNE reaches: two octaves either way)
        const auto pat = engine.pattern (lane);
        int stepNote = -1; juce::String stepName;
        for (int i = 0; i < pat.length && stepNote < 0; ++i)
            if (pat.steps[(size_t) i].on) { stepNote = (pat.steps[(size_t) i].octave + 2) * 12 + pat.steps[(size_t) i].note + keyMoves (lane); stepName = noteSays (lane, pat.steps[(size_t) i]); }
        const int st = stepNote >= 0 ? says.rootNote - stepNote : 0;
        if (stepNote >= 0 && std::abs (st) <= 24 && st != 0 && put ("tune", 0.5f + (float) st / 48.0f))
            told << ". " << stepName << " PLAYS IT AT ITS OWN PITCH (TUNE " << (st > 0 ? "+" : "") << st << " ST; ITS ROOT IS " << says.root << ")";
        else told << (stepNote >= 0 && st == 0 ? ". ITS STEPS ARE ON ITS ROOT, " : ". IT IS AT ITS OWN PITCH ON ") << says.root;
        told << ". A STEP PLAYS IT TO ITS END (PLAY: THROUGH)";
    }
    // (what PLAIN leaves alone that still colours it)
    juce::StringArray ins;
    for (int sl = 0; sl < kSlots; ++sl) if (const int ty = engine.insertType (lane, sl); ty >= 0 && engine.target (cx::insMute (lane, sl)) < 0.5f) ins.add (juce::String (effects()[(size_t) ty].name));
    if (! ins.isEmpty()) told << ". ITS INSERTS STILL ACT: " << ins.joinIntoString (", ");
    touch();
    if (onChange) onChange();
    return told;
}

void Session::newTrack()
{
    ensureCleanSlate();
    cue (0);
    if (learning >= 0) learnDrop();
    for (int k = 0; k < kMacroKnobs; ++k) if (engine.target (cx::kSnap + k) != 0.0f) userSet (cx::kSnap + k, 0.0f);        // (the macro knobs at rest: nothing is added to the scene)
    for (int l = 0; l < kLanes; ++l)
    {
        for (int which : { (int) cx::kMute, (int) cx::kSolo, (int) cx::kSeqMute }) if (engine.target (cx::lane (l, which)) > 0.5f) userSet (cx::lane (l, which), 0.0f);
        for (int sl = 0; sl < kSlots; ++sl)
        {
            if (engine.target (cx::insMute (l, sl)) > 0.5f) userSet (cx::insMute (l, sl), 0.0f);
            if (engine.target (cx::insSolo (l, sl)) > 0.5f) userSet (cx::insSolo (l, sl), 0.0f);
        }
    }
    for (int i = 0; i < 4; ++i) if (engine.target (cx::kRetMS + i) > 0.5f) userSet (cx::kRetMS + i, 0.0f);
    if (const auto* s = findSnap (kCleanSlateId))
    {
        stacksLeft (-1);
        const Snapshot copy = *s;
        applySnapshot (copy, 0.0);              // (at once, and with its steps: none)
        baseId = kCleanSlateId;
    }
    for (auto& v : heardLog) v.clear();         // (what came in from outside was the last track's)
    heardFrom = engine.songPos();
    selStep = -1; page = 0;
    touch();
    if (onChange) onChange();
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
        const bool some = indexOf (w.id) >= 0 && engine.heldHeard (h, w.stamp, 4, true);
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
    st = engine.grown (t, std::move (st));          // (a menu's place from before its list grew)
    bool motion = false, own = false;
    for (auto& kv : st) { motion = motion || Engine::isMotionId (kv.first); own = own || kv.first == "smp_gain"; }
    if (! motion) for (auto& kv : engine.motionOff (t)) st.push_back (kv);
    // (and one taken before a sample had controls of its own means them as they open: no gain, HELD, no envelope)
    if (! own) for (auto& kv : engine.sampleOwnOff (t)) st.push_back (kv);
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

// ------------------------------------------------------------------- effects, between two snapshots
// An effect is part of a sound, so a place between two snapshots has their effects between them too.
//  - The same effect in a slot of both: its knobs go between the two (a knob that turns; a menu or a switch is the
//    nearer one's, as is its CONFIG).
//  - An effect in one and nothing in the other: the effect, with its MIX as far up as the place is toward it. Half way
//    from dry to a reverb is half the reverb.
//  - Two different effects: one slot holds one effect, so the way from one to the other is through dry. The first
//    half of the way its MIX comes down to nothing; at half way the other takes the slot with its MIX at nothing, and
//    comes up over the second half.
// (Every effect of this rig has a MIX. One without would simply be the nearer snapshot's.)
int Session::mixCellOf (int type)
{
    if (type < 0 || type >= (int) effects().size()) return -1;
    const auto& d = effects()[(size_t) type];
    for (int c = 0; c < kCells; ++c) if (d.cells[c].label != nullptr && juce::String (d.cells[c].param) == "mix") return c;
    return -1;
}

// (a knob of an effect that turns: its place can be anywhere between two. A menu, a switch: one or the other. A key held: neither's.)
static char cellKind (int type, int c)
{
    if (type < 0 || type >= (int) effects().size() || effects()[(size_t) type].cells[c].label == nullptr) return 0;
    return effects()[(size_t) type].cells[c].kind;
}

Session::SlotSide Session::slotOf (const Snapshot* s, int l, int k) const
{
    SlotSide side;
    side.from = s;
    side.type = s != nullptr ? s->inserts[(size_t) l][(size_t) k] : engine.insertType (l, k);
    for (int c = 0; c < kCells; ++c)
    {
        const int i = cx::ins (l, k, c);
        side.cells[(size_t) c] = s != nullptr ? (i < (int) s->values.size() ? s->values[(size_t) i] : -1.0f) : (engine.control (i).used ? engine.target (i) : -1.0f);
    }
    return side;
}

Session::SlotSide Session::slotBetween (const SlotSide& a, const SlotSide& b, float f)
{
    f = juce::jlimit (0.0f, 1.0f, f);
    const SlotSide& near = f < 0.5f ? a : b;
    auto withMix = [] (SlotSide s, float share)
    {
        if (const int m = mixCellOf (s.type); m >= 0 && s.cells[(size_t) m] >= 0.0f) s.cells[(size_t) m] = juce::jlimit (0.0f, 1.0f, s.cells[(size_t) m] * share);
        for (int c = 0; c < kCells; ++c) if (cellKind (s.type, c) == 'h') s.cells[(size_t) c] = -1.0f;
        return s;
    };
    if (a.type == b.type)
    {
        SlotSide out = near;                                // (its menus, its switches, its CONFIG: the nearer one's. The rig as it stands: left as they are)
        for (int c = 0; c < kCells; ++c)
        {
            const char kind = cellKind (a.type, c);
            const float va = a.cells[(size_t) c], vb = b.cells[(size_t) c];
            if (kind == 'h') out.cells[(size_t) c] = -1.0f;
            else if (kind == 'u' || kind == 'b') out.cells[(size_t) c] = va < 0.0f ? vb : vb < 0.0f ? va : va + (vb - va) * f;
        }
        return out;
    }
    if (a.type < 0) return mixCellOf (b.type) >= 0 ? withMix (b, f) : (f >= 0.5f ? withMix (b, 1.0f) : a);
    if (b.type < 0) return mixCellOf (a.type) >= 0 ? withMix (a, 1.0f - f) : (f < 0.5f ? withMix (a, 1.0f) : b);
    return f < 0.5f ? withMix (a, mixCellOf (a.type) >= 0 ? 1.0f - 2.0f * f : 1.0f) : withMix (b, mixCellOf (b.type) >= 0 ? 2.0f * f - 1.0f : 1.0f);
}

// A slot brought to that. An effect that is already there stays, and its knobs travel; one that is not (or whose
// menus, switches or CONFIG are to be another's) comes whole, crossed over, as a recall brings one.
void Session::slotTo (int l, int k, const SlotSide& to, double glide)
{
    bool stays = engine.insertType (l, k) == to.type;
    if (stays && to.type < 0) return;
    if (stays && to.from != nullptr)
    {
        if (to.from->hasInsFiles && ! engine.insertHolds (l, k, to.type, to.from->insFiles[(size_t) l][(size_t) k])) stays = false;
        if (stays && to.from->hasSetup && setupDiffers (engine.getSetup ({ 1, l, k }), withMotion (engine, { 1, l, k }, to.from->insSetup[(size_t) l][(size_t) k]))) stays = false;
    }
    for (int c = 0; stays && c < kCells; ++c)
    {
        const int i = cx::ins (l, k, c);
        const auto& ct = engine.control (i);
        const float v = to.cells[(size_t) c];
        if (v >= 0.0f && ct.used && ! ct.glides && ct.kind != Kind::Hold && std::abs (v - engine.target (i)) > 1.0e-4f) stays = false;
    }
    if (! stays)
    {
        // (a key that is held, or a switch nothing says anything of: not held, off. The slot's last effect may have left anything there.)
        float cells[kCells];
        for (int c = 0; c < kCells; ++c) { const char kind = cellKind (to.type, c); cells[c] = to.cells[(size_t) c] < 0.0f && (kind == 'h' || kind == 't') ? 0.0f : to.cells[(size_t) c]; }
        const bool theirs = to.type >= 0 && to.from != nullptr;
        if (theirs && to.from->hasInsFiles) engine.expectInsertFiles (l, k, to.from->insFiles[(size_t) l][(size_t) k]);
        engine.setInsertType (l, k, to.type, false, theirs && to.from->hasSetup ? &to.from->insSetup[(size_t) l][(size_t) k] : nullptr, glide, cells);
        if (to.type < 0) return;
    }
    for (int c = 0; c < kCells; ++c)
    {
        const int i = cx::ins (l, k, c);
        const float v = to.cells[(size_t) c];
        if (v < 0.0f || ! engine.control (i).used) continue;
        base[(size_t) i] = v;
        engine.set (i, v, stays && engine.control (i).glides ? glide : 0.0);
    }
}

// CUE out, a stack knob turned a little toward a snapshot: the racks of the lanes that snapshot takes go that much of
// the way too. f: how much of what was left of the way this turn covered (as for the sound knobs). g0, g1: how far
// between the place behind the knob and the one ahead it stood, and stands now. Worked out from what is in the rack,
// so there is nothing to jump from.
void Session::sweepSlots (const Held& toward, float f, float g0, float g1)
{
    const auto sc = scopeOf (toward.lanes);
    const double glide = glideBeats();
    const bool arrived = f >= 1.0f - 1.0e-6f, crossed = g0 < 0.5f && g1 >= 0.5f;
    for (int l = 0; l < kLanes; ++l)
    {
        if (! sc.lane[(size_t) l]) continue;
        for (int k = 0; k < kSlots; ++k)
        {
            const SlotSide there = slotOf (toward.s, l, k);
            SlotSide here = slotOf (nullptr, l, k);
            if (here.type < 0 && there.type < 0) continue;
            const int hm = mixCellOf (here.type), tm = mixCellOf (there.type);
            if (here.type == there.type)
            {
                // the same effect: its knobs go that much of the way; its menus, switches and CONFIG are the snapshot's from half way on
                bool moved = false;
                for (int c = 0; c < kCells; ++c)
                {
                    const char kind = cellKind (here.type, c);
                    const float hv = here.cells[(size_t) c], tv = there.cells[(size_t) c];
                    if (kind == 'h') { here.cells[(size_t) c] = -1.0f; continue; }
                    if (tv < 0.0f || hv < 0.0f) continue;
                    if (kind == 'u' || kind == 'b') { if (std::abs (tv - hv) > 1.0e-6f) { here.cells[(size_t) c] = hv + (tv - hv) * f; moved = true; } }
                    else if (g1 >= 0.5f && std::abs (tv - hv) > 1.0e-4f) { here.cells[(size_t) c] = tv; moved = true; }
                }
                if (crossed || arrived) here.from = toward.s;
                if (moved || here.from != nullptr) slotTo (l, k, here, glide);
            }
            else if (here.type < 0)
            {
                // nothing there, an effect ahead: it is put in with its MIX at nothing, and comes up as the knob goes
                if (tm >= 0) slotTo (l, k, slotBetween (here, there, f), glide);
                else if (g1 >= 0.5f) slotTo (l, k, there, glide);
            }
            else if (there.type < 0)
            {
                // an effect there, nothing ahead: its MIX comes down as the knob goes, and at the snapshot's place it is taken out
                if (arrived || (hm < 0 && g1 >= 0.5f)) slotTo (l, k, there, glide);
                else if (hm >= 0 && here.cells[(size_t) hm] >= 0.0f) { here.cells[(size_t) hm] *= 1.0f - f; for (int c = 0; c < kCells; ++c) if (c != hm) here.cells[(size_t) c] = -1.0f; slotTo (l, k, here, glide); }
            }
            else if (g1 < 0.5f && hm >= 0)
            {
                // another effect ahead, and the knob short of half way: this one's MIX comes down, to be nothing at half way
                if (here.cells[(size_t) hm] >= 0.0f) { here.cells[(size_t) hm] *= (0.5f - g1) / std::max (1.0e-6f, 0.5f - g0); for (int c = 0; c < kCells; ++c) if (c != hm) here.cells[(size_t) c] = -1.0f; slotTo (l, k, here, glide); }
            }
            else if (g1 >= 0.5f && (g0 < 0.5f || hm < 0 || here.cells[(size_t) hm] <= 1.0e-4f || arrived))
            {
                // half way, or past it with this one's MIX at nothing: the one ahead takes the slot, its MIX as far up as the knob is past half way
                slotTo (l, k, slotBetween (SlotSide(), there, tm >= 0 ? juce::jlimit (0.0f, 1.0f, (g1 - 0.5f) * 2.0f) : 1.0f), glide);
            }
            else if (hm >= 0 && here.cells[(size_t) hm] >= 0.0f)
            {
                // past half way with another effect still sounding (the set came here some other way): its MIX comes down as the
                // knob goes, so nothing jumps, and at the snapshot's place the one ahead takes the slot
                here.cells[(size_t) hm] *= 1.0f - f; for (int c = 0; c < kCells; ++c) if (c != hm) here.cells[(size_t) c] = -1.0f; slotTo (l, k, here, glide);
            }
        }
    }
}

void Session::applySnapshot (const Snapshot& s, double glide, const std::array<bool, kLanes>* notes)
{
    // What can travel, travels: sound knobs glide to their places. What cannot (an effect put in or
    // taken out, a model, a menu, a switch) is crossed over instead, over the same time: the one
    // that was there carries on as it is and fades out while the new one arrives beside it.
    // 1. the lanes whose instrument is to change that way: the one playing leaves as it stands, and
    //    a copy of it takes its place to be changed by what follows
    //    (with the audio not yet started there is nothing playing to leave: the change is simply made)
    settleHolds();                  // (a lane a stack knob was holding between two instruments: the one that arrived is its instrument)
    if (engine.isPrepared()) for (int l = 0; l < kLanes; ++l) if (laneJumps (s, l)) engine.forkVoice (l, glide);
    // (and each lane's model before its knobs: a model with sound knobs of its own names what the first eight places mean,
    // so the values that follow reach what this snapshot meant by them)
    if (s.hasSetup)
        for (int l = 0; l < kLanes; ++l)
            if (! lanes()[(size_t) l].own.empty())
                for (auto& kv : s.setup[(size_t) l]) if (kv.first == "model") { engine.applySetup ({ 0, l, 0 }, { kv }); break; }
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
        // A snapshot holds each knob as it SOUNDED (capture), with whatever the macro knobs were adding in it. So what
        // it is put back to is what sounds, as a hand's turn is (userSet): the scene under the macro knobs is set so
        // that, with what they add now, the knob is where the snapshot has it. (It was set to the snapshot's value
        // itself, and the macro knobs then added to that again: a snapshot recalled with ENERGY still up was not the
        // sound its clip has, and one taken and recalled at once was pushed twice.)
        base[(size_t) i] = morph ? v - offset[(size_t) i] : v;
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
        stacksLeft (-1);
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
    // (the log: a recall that a hand asked for, here or on a controller. Not the arrangement's own, which logs its parts)
    if (glideOver < 0.0 && findSnap (id) != nullptr)
    {
        juce::NamedValueSet how { { "parts", (int) lanesOf (scope) }, { "notes", scope.notes }, { "base", uidOf (baseId) } };
        if (logFrom != nullptr) { how.set ("from", logFrom); if (logKnob >= 0) how.set ("knob", logKnob + 1); }
        logAction ("recall", id, how);
    }
    // (ARM: it waits for the line. Not a recall that is made over a given time: the arrangement's own, and ARM's when its line comes.)
    if (glideOver < 0.0) { TransPend p; p.kind = 0; p.id = id; p.scope = scope; if (transDefer (p)) return; }
    if (scope.all()) { recallWhole (id); return; }
    auto* s = findSnap (id);
    if (s == nullptr) return;
    stacksLeft (-1);
    // The snapshot, with every part that is not chosen put back as it stands now: applied whole,
    // those parts find nothing to change.
    Snapshot m = *s;
    const Snapshot now = capture();
    if (m.values.size() < (size_t) cx::kCount) m.values.resize ((size_t) cx::kCount, -1.0f);
    auto keep = [&] (int from, int to)
    {
        for (int i = from; i < to; ++i)
            m.values[(size_t) i] = now.values[(size_t) i] < 0.0f || (engine.control (i).morph && i < kMorphEnd) ? -1.0f : now.values[(size_t) i];      // (a sound knob of a part not chosen: nothing is said of it, and the scene under the macro knobs stays as it is)
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
    logAction ("delete", id);
    library.erase (library.begin() + i);
    waiting.erase (std::remove_if (waiting.begin(), waiting.end(), [id] (const Waiting& w) { return w.id == id; }), waiting.end());
    if (cueId == id) cue (0);
    if (useDisk) { waitForClips(); clipFile (id).deleteFile(); stemsFile (id).deleteFile(); losslessFile (id).deleteFile(); }
    for (auto& k : knobs) { k.drop (id); if (k.picked == id) k.picked = 0; }
    {
        const auto partsWere = arrangement.size();
        arrangement.erase (std::remove_if (arrangement.begin(), arrangement.end(), [id] (const ArrPart& p) { return p.snap == id; }), arrangement.end());      // (the parts it was, in the arrangement)
        if (arrangement.size() != partsWere) arrEdit = -1;      // (and an EDIT of one of its parts: the parts are not where they were)
    }
    for (auto& st : stacks)                                             // (and out of the stacks: those LOAD brought, those kept here; its parts go with it)
        for (size_t i = st.snaps.size(); i-- > 0;)
            if (st.snaps[i] == id) { st.snaps.erase (st.snaps.begin() + (long) i); if (i < st.lanes.size()) st.lanes.erase (st.lanes.begin() + (long) i); }
    stacks.erase (std::remove_if (stacks.begin(), stacks.end(), [] (const Stack& st) { return st.here && st.snaps.empty(); }), stacks.end());
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
    // A rhythm changed by hand is the hand's from then on: the generator lets go of it, so that a
    // turn of HITS does not write over what was just made. (A note or an octave is not rhythm, and
    // nor is the pattern's length: the generator stays.)
    if (genType[(size_t) l] >= 0)
    {
        Pattern was = engine.pattern (l);
        was.length = p.length;
        if (! sameRhythm (was, p)) genType[(size_t) l] = -1;
    }
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
    const Pattern was = p;
    generate (p, g);
    // (a step the generator lights that held nothing is a new step: on a lane that does not follow the key it is
    // written at the key's note, as one lit by hand is. A step that kept a note under the generator keeps it.)
    if (const int k = keyRoot(); k > 0 && lanes()[(size_t) l].pitched && ! engine.laneFollowsKey (l))
    {
        const int oct = lanes()[(size_t) l].octave;
        for (int i = 0; i < p.length; ++i)
            if (auto& st = p.steps[(size_t) i]; st.on && ! was.steps[(size_t) i].on && st.note == 0 && st.octave == oct)
                { st.note = k; if (k > 5) st.octave = std::max (0, oct - 1); }
    }
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
    auto light = [&] { if (! s.on) lightStep (l, s); };
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
    if (! s.on) lightStep (l, s); else clearStep (s, oct);
    setPattern (l, p);
    if (onChange) onChange();
}

int Session::keyRoot() const { return (int) std::lround (value (cx::kKey) * 12.0f) - 1; }

int Session::keyMoves (int l) const
{
    const int k = keyRoot();
    if (k < 0 || ! lanes()[(size_t) l].pitched || ! engine.laneFollowsKey (l)) return 0;
    return k > 5 ? k - 12 : k;
}

void Session::lightStep (int l, Step& s) const
{
    // A lane that follows the key: written C, which the key moves to its own note. One that does
    // not (KEY FOLLOW off in its CONFIG) is written at the key's note, the same one a lane that
    // follows would sound: nothing will move it there.
    const auto& d = lanes()[(size_t) l];
    clearStep (s, d.octave);
    s.on = true;
    if (const int k = keyRoot(); k >= 0 && d.pitched && ! engine.laneFollowsKey (l))
    {
        s.note = k;
        if (k > 5) s.octave = std::max (0, d.octave - 1);
    }
}

juce::String Session::noteSays (const Step& s, int moves)
{
    const int n = juce::jlimit (0, 127, 12 * (s.octave + 2) + juce::jlimit (0, 11, s.note) + moves);
    return juce::String (kNoteNames[n % 12]) + juce::String (n / 12 - 2);
}

juce::String Session::stepSays() const
{
    const Pattern p = pattern (activeLane);
    if (selStep < 0 || selStep >= p.length) return {};
    const auto& s = p.steps[(size_t) selStep];
    juce::String t (selStep + 1);
    if (! s.on) return t + " OUT";
    if (lanes()[(size_t) activeLane].pitched) t << " " << noteSays (activeLane, s);
    return t;
}

void Session::pressGenerator (int l, int type)
{
    // RANDOM: a new pattern at every press. SEED goes to a new place, so the four knobs still say
    // what is playing, and it stays armed.
    // EUCLID: a press writes the pattern at once and arms the knobs; a press on the lit key lets go
    // of them (it has nothing new to write).
    if (type == 0)
    {
        genType[(size_t) l] = 0;
        // (any of SEED's other 127 places; and one whose rhythm is not the one playing, if HITS
        // leaves it a choice: with no hits, or every step, there is only the one)
        const Pattern was = engine.pattern (l);
        GenParams g;
        g.type = 0;
        g.hits = engine.target (cx::gen (l, 0)); g.rotate = engine.target (cx::gen (l, 1)); g.accents = engine.target (cx::gen (l, 2));
        const int at = juce::jlimit (0, 127, (int) std::lround (engine.target (cx::gen (l, 3)) * 127.0f));
        int next = at;
        for (int tries = 0; tries < 32; ++tries)
        {
            next = juce::Random::getSystemRandom().nextInt (127);
            if (next >= at) ++next;
            Pattern p = was;
            g.seed = (float) next / 127.0f;
            generate (p, g);
            if (! sameRhythm (was, p)) break;
        }
        userSet (cx::gen (l, 3), (float) next / 127.0f);        // (armed: this writes the pattern)
    }
    else if (genType[(size_t) l] == type) genType[(size_t) l] = -1;
    else { genType[(size_t) l] = type; regenerate (l); }
    touch();
}

void Session::nudgeLane (int l, int dir)
{
    Pattern p = engine.pattern (l);
    nudge (p, dir);
    genType[(size_t) l] = -1;                   // (moved by hand: the generator lets go, as for any rhythm made by hand)
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

// A library, from one folder to another: the session, its id, what has been sent online, the clips,
// the samples and the log. (Not the backups: they are of the place they were made in.)
static bool copyLibrary (const juce::File& from, const juce::File& to)
{
    if (! to.createDirectory()) return false;
    bool ok = true;
    for (auto* name : { "session.json", "library-id.txt", "online-sent.json" })
        if (const auto f = from.getChildFile (name); f.existsAsFile()) ok = f.copyFileTo (to.getChildFile (name)) && ok;
    for (auto* name : { "clips", "samples", "log" })
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

void Session::placeForRig (const juce::File& libraryDir)
{
    gPlaces = {}; gPlacesRead = true; gSettled = true;
    gLibraryDir = libraryDir;
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
    use (engine.transFile (0)); use (engine.transFile (1));        // (TRANSITION's two)
    for (auto& p : arrangement) { use (p.inFile); use (p.hitFile); }  // (and each part's own)
    for (auto& a : arrs)                                              // (and those of the arrangements kept that are not on the page)
        if (a.id != arrAtId)
            if (auto* parts = a.parts.getProperty ("parts", var()).getArray())
                for (auto& pv : *parts) { use (pathOf (pv.getProperty ("inFile", "").toString())); use (pathOf (pv.getProperty ("hitFile", "").toString())); }
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
    o->setProperty ("lists", Session::kListsNow);   // (which lists of models, and which knobs of theirs, it was taken on: fromListsBefore)
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

// ------------------------------------------------------------------------------- LOAD
static Snapshot snapFromVar (const var& v, const Engine& e);
// A name as this library keeps one: capitals, twenty-four letters.
static juce::String nameHere (const juce::String& n) { return n.trim().toUpperCase().substring (0, 24); }

juce::int64 Session::takeFromLibrary (const var& snap, const juce::String& name, const var& defs)
{
    const juce::String uid = snap.getProperty ("uid", "").toString();
    if (! snap.isObject() || uid.isEmpty()) return 0;
    for (auto& s : library)
        if (s.uid == uid)
        {
            if (nameHere (name).isNotEmpty() && s.name != nameHere (name)) rename (s.id, name);       // (named there since: its name is the one thing that may change)
            return s.id;
        }
    // What is kept is this library's own copy: its id here (its own, if that is free), its name.
    var v = juce::JSON::parse (juce::JSON::toString (snap, true));
    auto* o = v.getDynamicObject();
    if (o == nullptr) return 0;
    juce::int64 id = (juce::int64) snap.getProperty ("id", 0);
    if (id < 1000000 || findSnap (id) != nullptr) { id = juce::Time::currentTimeMillis(); while (findSnap (id) != nullptr) ++id; }
    o->setProperty ("id", id);
    if (nameHere (name).isNotEmpty()) o->setProperty ("name", nameHere (name));
    Snapshot s = snapFromVar (v, engine);
    for (auto& f : s.laneFiles) f = pathOf (f);
    for (auto& lane : s.insFiles) for (auto& slot : lane) for (auto& r : slot) for (auto& f : r.files) f = pathOf (f);
    library.push_back (s);
    if (auto* d = defs.getDynamicObject())
        for (auto& p : d->getProperties())
            if (! definition (p.name.toString()).isObject()) defsKept.push_back ({ p.name.toString(), p.value });
    touch();
    if (onLibrary) onLibrary();
    return id;
}

int Session::filesMissing (juce::int64 id) const
{
    const auto* s = findSnap (id);
    if (s == nullptr) return 0;
    int n = 0;
    auto miss = [&n] (const juce::String& f) { if (f.isNotEmpty() && ! juce::File (f).existsAsFile()) ++n; };
    for (auto& f : s->laneFiles) miss (f);
    for (auto& lane : s->insFiles) for (auto& slot : lane) for (auto& r : slot) for (auto& f : r.files) miss (f);
    return n;
}

// ---- a knob as the web library keeps one
bool Session::knobHolds (int k) const { const auto& kn = knobs[(size_t) k]; return isCorner (k) ? false : isStack (k) ? ! kn.snaps.empty() : ! kn.links.empty(); }

bool Session::knobStaysHere (int k) const
{
    if (k < 0 || k >= 16 || ! isStack (k)) return false;
    for (auto id : knobs[(size_t) k].snaps)
        if (const auto* sn = findSnap (id); sn == nullptr || sn->uid.isEmpty() || id < 1000000) return true;       // (below a million: the rig's own, which ONLINE never sends. Online::kFirstTaken)
    return false;
}

static juce::String hereId();
int Session::stackKeep (int k)
{
    if (! isStack (k) || ! knobHolds (k)) return -1;
    const auto& kn = knobs[(size_t) k];
    Stack st;
    st.name = kn.name.isNotEmpty() ? kn.name : "STACK " + juce::String (k + 1);
    st.kind = "stack"; st.here = true;
    for (size_t i = 0; i < kn.snaps.size(); ++i)
        if (findSnap (kn.snaps[i]) != nullptr) { st.snaps.push_back (kn.snaps[i]); st.lanes.push_back (kn.keysAt (i)); }
    if (st.snaps.empty()) return -1;
    bool all = true;
    for (auto l : st.lanes) all = all && l == kAllLanes;
    if (all) st.lanes.clear();
    int at = -1;
    for (int i = 0; i < (int) stacks.size() && at < 0; ++i) if (stacks[(size_t) i].here && stacks[(size_t) i].name == st.name) at = i;
    if (at >= 0) { st.id = stacks[(size_t) at].id; st.mark = stacks[(size_t) at].mark; st.kept = stacks[(size_t) at].kept; stacks[(size_t) at] = st; }     // (the same stack, as it is now: the web library's copy follows)
    else
    {
        st.id = hereId();
        stacks.push_back (st);
        at = (int) stacks.size() - 1;
    }
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
    return at;
}

bool Session::stackOnto (int index, int k)
{
    if (! isStack (k) || index < 0 || index >= (int) stacks.size()) return false;
    const auto st = stacks[(size_t) index];
    auto& kn = knobs[(size_t) k];
    if ((int) (kn.snaps.size() + st.snaps.size()) > kStackMost) return false;
    const bool empty = kn.snaps.empty();
    bool any = false;
    {
        const Quiet q (*this);          // (one thing done: a stack onto a knob, logged once)
        for (size_t i = 0; i < st.snaps.size(); ++i) any = stackAdd (k, st.snaps[i], st.keysAt (i)) || any;
    }
    if (any) logAction ("onto", 0, { { "stack", st.id }, { "name", st.name }, { "knob", k + 1 }, { "chips", (int) st.snaps.size() } });
    if (any && empty && kn.name.isEmpty()) { kn.name = st.name.substring (0, 24); touch(); if (onChange) onChange(); }
    return any;
}

// ---- stacks of their own
static juce::String hereId()
{
    juce::String id ("here");
    juce::Random r;
    for (int i = 0; i < 12; ++i) id << juce::String::charToString ((juce::juce_wchar) "abcdefghijklmnopqrstuvwxyz0123456789"[r.nextInt (36)]);
    return id;
}

int Session::rackIndex (const juce::String& id) const
{
    if (id.isNotEmpty()) for (int i = 0; i < (int) stacks.size(); ++i) if (stacks[(size_t) i].id == id) return i;
    return -1;
}

bool Session::rackCanChange (int index) const
{
    return index >= 0 && index < (int) stacks.size() && stacks[(size_t) index].kind != "song";
}

int Session::rackNew (const juce::String& wanted)
{
    auto taken = [this] (const juce::String& n) { for (auto& st : stacks) if (st.name == n) return true; return false; };
    juce::String want = wanted.trim().toUpperCase().substring (0, 24);
    if (want.isEmpty()) want = "STACK " + juce::String ((int) stacks.size() + 1);
    juce::String name = want;
    for (int n = 2; taken (name); ++n) name = want.substring (0, 21) + " " + juce::String (n);
    Stack st;
    st.id = hereId(); st.name = name; st.kind = "stack"; st.here = true;
    stacks.push_back (st);
    touch();
    if (onChange) onChange();
    return (int) stacks.size() - 1;
}

bool Session::rackAdd (int index, juce::int64 id, juce::uint32 lanes)
{
    if (! rackCanChange (index) || findSnap (id) == nullptr) return false;
    auto& st = stacks[(size_t) index];
    lanes = keysOf (lanes);
    if (lanes != kAllLanes || ! st.lanes.empty()) { st.lanes.resize (st.snaps.size(), kAllLanes); st.lanes.push_back (lanes); }
    st.snaps.push_back (id);
    logAction ("add", id, { { "to", "stack" }, { "stack", st.id }, { "name", st.name }, { "at", (int) st.snaps.size() - 1 }, { "parts", (int) (lanes & kAllLanes) } });
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
    return true;
}

void Session::rackDrop (int index, int at)
{
    if (! rackCanChange (index)) return;
    auto& st = stacks[(size_t) index];
    if (at < 0 || at >= (int) st.snaps.size()) return;
    logAction ("drop", st.snaps[(size_t) at], { { "from", "stack" }, { "stack", st.id }, { "name", st.name }, { "at", at } });
    st.snaps.erase (st.snaps.begin() + at);
    if (at < (int) st.lanes.size()) st.lanes.erase (st.lanes.begin() + at);
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
}

void Session::rackSwap (int index, int a, int b)
{
    if (! rackCanChange (index)) return;
    auto& st = stacks[(size_t) index];
    const int n = (int) st.snaps.size();
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
    if (! st.lanes.empty()) { st.lanes.resize ((size_t) n, kAllLanes); std::swap (st.lanes[(size_t) a], st.lanes[(size_t) b]); }
    std::swap (st.snaps[(size_t) a], st.snaps[(size_t) b]);
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
}

void Session::rackMove (int index, int from, int to)
{
    if (! rackCanChange (index)) return;
    auto& st = stacks[(size_t) index];
    const int n = (int) st.snaps.size();
    if (from < 0 || from >= n) return;
    to = juce::jlimit (0, n - 1, to);
    if (to == from) return;
    if (! st.lanes.empty())
    {
        st.lanes.resize ((size_t) n, kAllLanes);
        const auto l = st.lanes[(size_t) from];
        st.lanes.erase (st.lanes.begin() + from); st.lanes.insert (st.lanes.begin() + to, l);
    }
    const auto id = st.snaps[(size_t) from];
    logAction ("move", id, { { "in", "stack" }, { "stack", st.id }, { "name", st.name }, { "at", from }, { "to", to } });
    st.snaps.erase (st.snaps.begin() + from); st.snaps.insert (st.snaps.begin() + to, id);
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
}

bool Session::rackInsert (int index, int at, juce::int64 id, juce::uint32 lanes)
{
    if (! rackCanChange (index)) return false;
    const int n = (int) stacks[(size_t) index].snaps.size();
    {
        const Quiet q (*this);          // (one thing done: logged once, with the place it went to)
        if (! rackAdd (index, id, lanes)) return false;
        if (at >= 0 && at < n) rackMove (index, n, at);
    }
    const auto& st = stacks[(size_t) index];
    logAction ("add", id, { { "to", "stack" }, { "stack", st.id }, { "name", st.name }, { "at", at >= 0 && at < n ? at : n }, { "parts", (int) (keysOf (lanes) & kAllLanes) } });
    return true;
}

int Session::freeStackKnob() const
{
    for (int k = 0; k < 16; ++k) if (isStack (k) && ! knobHolds (k)) return k;
    return -1;
}

int Session::rackOntoFree (int index)
{
    if (index < 0 || index >= (int) stacks.size() || stacks[(size_t) index].snaps.empty()) return -1;
    const int k = freeStackKnob();
    if (k < 0) return -1;
    knobs[(size_t) k].name = {};                // (a place that holds nothing may still have a name from what it held: the stack's is the one it takes)
    return stackOnto (index, k) ? k : -1;
}

void Session::rackLanes (int index, int at, juce::uint32 lanes)
{
    if (! rackCanChange (index)) return;
    auto& st = stacks[(size_t) index];
    if (at < 0 || at >= (int) st.snaps.size()) return;
    st.lanes.resize (st.snaps.size(), kAllLanes);
    st.lanes[(size_t) at] = keysOf (lanes);
    bool all = true;
    for (auto l : st.lanes) all = all && l == kAllLanes;
    if (all) st.lanes.clear();
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
}

int Session::rackCopy (int index)
{
    if (index < 0 || index >= (int) stacks.size()) return -1;
    const Stack from = stacks[(size_t) index];
    const int at = rackNew (from.name);                 // (an empty one of the instrument's own, under the next free name)
    {
        const Quiet q (*this);          // (a copy is not each of its chips chosen again)
        for (size_t i = 0; i < from.snaps.size(); ++i) rackAdd (at, from.snaps[i], from.keysAt (i));
    }
    return at;
}

void Session::keptRename (int index, const juce::String& wanted)
{
    if (index < 0 || index >= (int) keptKnobs.size()) return;
    const auto name = wanted.trim().toUpperCase().substring (0, 24);
    if (name.isEmpty()) return;
    if (auto* o = keptKnobs[(size_t) index].knob.getDynamicObject()) o->setProperty ("name", name);
    touch();
    if (onChange) onChange();
}

int Session::keptOntoFree (int index)
{
    if (index < 0 || index >= (int) keptKnobs.size()) return -1;
    for (int k = 0; k < 16; ++k)
        if (! isCorner (k) && isStack (k) == keptKnobs[(size_t) index].stack && ! knobHolds (k))
            return knobPut (index, k) == putDone ? k : -1;
    return -1;
}

void Session::rackRename (int index, const juce::String& wanted)
{
    if (! rackCanChange (index)) return;
    const auto name = wanted.trim().toUpperCase().substring (0, 24);
    if (name.isEmpty() || name == stacks[(size_t) index].name) return;
    stacks[(size_t) index].name = name;
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
}

void Session::stackForget (int index)
{
    if (index < 0 || index >= (int) stacks.size()) return;
    if (const auto& st = stacks[(size_t) index]; st.mark.isNotEmpty())
    {
        // (the web library has it: ONLINE takes it out of the rack there too, if it is still as it was)
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", st.id); o->setProperty ("mark", st.mark); o->setProperty ("here", st.here);
        stacksGone.add (var (o));
    }
    stacks.erase (stacks.begin() + index);
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
}

// ---- modules
juce::var Session::knobVar (const MacroKnob& k) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", k.name);
    o->setProperty ("depth", k.depth);
    juce::Array<var> ids;
    for (auto id : k.snaps) ids.add (id);
    o->setProperty ("snaps", ids);
    {
        // a stack knob's snapshots, each with the parts of the rig it takes (said only where one does not take them all)
        bool some = false;
        // (and its N, the key after the parts: FORMAT.md)
        for (size_t i = 0; i < k.snaps.size(); ++i) some = some || k.keysAt (i) != kAllLanes;
        if (some) { juce::Array<var> ln; for (size_t i = 0; i < k.snaps.size(); ++i) ln.add ((int) k.keysAt (i)); o->setProperty ("lanes", ln); }
        if (stackCue && k.sweptAt != 0.0f) o->setProperty ("at", k.sweptAt);
    }
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
    return var (o);
}

MacroKnob Session::knobFrom (const var& o) const
{
    MacroKnob r;
    r.name = o.getProperty ("name", "").toString();
    r.depth = juce::jlimit (0.0f, 2.0f, (float) o.getProperty ("depth", 1.0));
    const var ln = o.getProperty ("lanes", var());
    if (auto* ids = o.getProperty ("snaps", var()).getArray())
        for (int i = 0; i < ids->size(); ++i)
            if (findSnap ((juce::int64) (*ids)[i]) != nullptr)
                r.add ((juce::int64) (*ids)[i], ln.isArray() && i < ln.size() ? ((juce::uint32) (int) ln[i] & kAllKeys) : MacroKnob::kAll);
    if (auto* ls = o.getProperty ("links", var()).getArray())
        for (auto& l : *ls)
        {
            const int i = engine.find (l.getProperty ("c", "").toString());
            const float d = juce::jlimit (-1.0f, 1.0f, (float) (double) l.getProperty ("d", 0.0));
            if (reaches (engine, i)) { if (std::abs (d) >= 0.004f) r.links.push_back ({ i, d }); }
            else r.linksKept.add (l);
        }
    if (auto* ko = o.getDynamicObject())
        for (auto& p : ko->getProperties())
        {
            static const char* const mine[] = { "name", "depth", "snaps", "links", "pick", "lanes", "at" };
            bool is = false;
            for (auto* m : mine) is = is || p.name == juce::Identifier (m);
            if (! is) r.more.set (p.name, p.value);
        }
    return r;
}

juce::var Session::moduleKnobsNow() const
{
    juce::Array<var> kn;
    for (auto& k : knobs) kn.add (knobVar (k));
    return var (kn);
}

juce::var Session::moduleMotionNow() const
{
    auto* o = new juce::DynamicObject();
    for (int i = cx::kMot; i < cx::kSnap; ++i) o->setProperty (engine.control (i).id, engine.target (i));
    return var (o);
}

// ---- the knobs kept
int Session::knobKeep (int k)
{
    if (k < 0 || k >= 16 || isCorner (k) || ! knobHolds (k)) return -1;
    KeptKnob kk;
    kk.stack = isStack (k);
    MacroKnob copy = knobs[(size_t) k];
    if (copy.name.isEmpty()) copy.name = (kk.stack ? "STACK " : "MACRO ") + juce::String (k + 1);
    copy.picked = 0; copy.sweptAt = 0.0f;
    // (a copy is its own: not the web library's module that the knob on the module is)
    copy.more.remove ("lib"); copy.more.remove ("libmark"); copy.more.remove ("reseat");
    kk.knob = knobVar (copy);
    int at = -1;
    for (int i = 0; i < (int) keptKnobs.size() && at < 0; ++i) if (keptKnobs[(size_t) i].stack == kk.stack && keptKnobs[(size_t) i].name() == copy.name) at = i;
    if (at >= 0) { kk.id = keptKnobs[(size_t) at].id; keptKnobs[(size_t) at] = kk; }      // (kept again under its name: the same one, as it is now)
    else
    {
        kk.id = "knob";
        juce::Random r;
        for (int i = 0; i < 12; ++i) kk.id << juce::String::charToString ((juce::juce_wchar) "abcdefghijklmnopqrstuvwxyz0123456789"[r.nextInt (36)]);
        keptKnobs.push_back (kk);
        at = (int) keptKnobs.size() - 1;
    }
    touch();
    if (onChange) onChange();
    return at;
}

Session::PutSays Session::knobPut (int index, int k)
{
    if (index < 0 || index >= (int) keptKnobs.size() || k < 0 || k >= 16 || isCorner (k)) return putWrongKind;
    const auto& kk = keptKnobs[(size_t) index];
    if (kk.stack != isStack (k)) return putWrongKind;
    if (knobHolds (k)) return putHolds;
    MacroKnob made = knobFrom (kk.knob);
    if (kk.stack ? made.snaps.empty() : made.links.empty()) return putNothing;      // (its snapshots are gone from the library, or this rig has none of the knobs it was tied to)
    if (learning == k) learnDrop();
    made.picked = 0; made.sweptAt = 0.0f;
    knobs[(size_t) k] = made;
    engine.set (cx::kSnap + k, 0.0f);           // (it starts at rest)
    base[(size_t) (cx::kSnap + k)] = 0.0f;
    applyMorph (0.0);
    lawsSig = 0; motionLaws();
    touch();
    if (onChange) onChange();
    return putDone;
}

void Session::knobForget (int index)
{
    if (index < 0 || index >= (int) keptKnobs.size()) return;
    keptKnobs.erase (keptKnobs.begin() + index);
    touch();
    if (onChange) onChange();
}

juce::String Session::keptSays (int index) const
{
    if (index < 0 || index >= (int) keptKnobs.size()) return {};
    const auto& kk = keptKnobs[(size_t) index];
    juce::String t;
    if (kk.stack)
    {
        int n = 0;
        juce::String names;
        if (auto* ids = kk.knob.getProperty ("snaps", var()).getArray())
            for (auto& id : *ids) if (const auto* sn = findSnap ((juce::int64) id)) { names << (n > 0 ? " > " : "") << sn->name; ++n; }
        t << "STACK KNOB, " << n << (n == 1 ? " SNAPSHOT: " : " SNAPSHOTS: ") << names;
    }
    else
    {
        const int n = kk.knob.getProperty ("links", var()).size();
        t << "MACRO KNOB, TIED TO " << n << (n == 1 ? " KNOB" : " KNOBS");
    }
    return t;
}

static juce::String moduleNamed (juce::String want, const std::vector<Module>& all, int but = -1)
{
    want = want.trim().toUpperCase().substring (0, 24);
    if (want.isEmpty()) want = "MODULE " + juce::String ((int) all.size() + 1);
    auto taken = [&] (const juce::String& n) { for (int i = 0; i < (int) all.size(); ++i) if (i != but && all[(size_t) i].name == n) return true; return false; };
    juce::String name = want;
    for (int n = 2; taken (name); ++n) name = want.substring (0, 21) + " " + juce::String (n);
    return name;
}

static juce::String moduleId()
{
    juce::String id ("mod");
    juce::Random r;
    for (int i = 0; i < 13; ++i) id << juce::String::charToString ((juce::juce_wchar) "abcdefghijklmnopqrstuvwxyz0123456789"[r.nextInt (36)]);
    return id;
}

int Session::moduleIndex() const
{
    for (int i = 0; i < (int) modules.size(); ++i) if (modules[(size_t) i].id == moduleAt) return i;
    return -1;
}

juce::String Session::moduleName() const { const int i = moduleIndex(); return i >= 0 ? modules[(size_t) i].name : juce::String(); }

void Session::modulesReady()
{
    if (moduleIndex() >= 0) return;
    // (a session from before modules, or a new one: the knobs it has are its first module)
    Module m;
    m.id = moduleId(); m.name = moduleNamed ("MODULE 1", modules);
    m.knobs = moduleKnobsNow(); m.motion = moduleMotionNow();
    modules.push_back (m);
    moduleAt = m.id;
}

int Session::moduleCopy (const juce::String& name)
{
    modulesReady();
    const int was = moduleIndex();
    modules[(size_t) was].knobs = moduleKnobsNow(); modules[(size_t) was].motion = moduleMotionNow();       // (the one it is a copy of, kept as it stands)
    Module m;
    m.id = moduleId();
    m.name = moduleNamed (name.isNotEmpty() ? name : modules[(size_t) was].name, modules);
    modules.push_back (m);
    moduleAt = m.id;
    // (the copy is its own: a knob of it is not the web library's module that the first one's knob is)
    for (auto& k : knobs) { k.more.remove ("lib"); k.more.remove ("libmark"); k.more.remove ("reseat"); }
    modules.back().knobs = moduleKnobsNow(); modules.back().motion = moduleMotionNow();
    touch();
    if (onChange) onChange();
    return (int) modules.size() - 1;
}

int Session::moduleNew (const juce::String& name)
{
    modulesReady();
    Module m;
    m.id = moduleId(); m.name = moduleNamed (name, modules);
    juce::Array<var> kn;
    for (int k = 0; k < 16; ++k) kn.add (knobVar (MacroKnob()));
    m.knobs = var (kn);
    modules.push_back (m);
    moduleLoad ((int) modules.size() - 1);
    return (int) modules.size() - 1;
}

bool Session::moduleLoad (int index)
{
    modulesReady();
    const int was = moduleIndex();
    if (index < 0 || index >= (int) modules.size() || index == was) return false;
    modules[(size_t) was].knobs = moduleKnobsNow(); modules[(size_t) was].motion = moduleMotionNow();       // (the one in the chassis, kept as it stands)
    if (learning >= 0) learnDrop();
    cue (0);
    // What the macro knobs were adding stays in the sound: it becomes the scene, so nothing is heard to change as the
    // knobs that were adding it leave.
    for (int i = 0; i < kMorphEnd; ++i) if (engine.control (i).morph && offset[(size_t) i] != 0.0f) base[(size_t) i] = juce::jlimit (0.0f, 1.0f, base[(size_t) i] + offset[(size_t) i]);
    const Module m = modules[(size_t) index];
    for (int k = 0; k < 16; ++k)
    {
        knobs[(size_t) k] = m.knobs.isArray() && k < m.knobs.size() && ! isCorner (k) ? knobFrom (m.knobs[k]) : MacroKnob();
        knobs[(size_t) k].picked = 0; knobs[(size_t) k].sweptAt = 0.0f;
        engine.set (cx::kSnap + k, 0.0f);       // (its knobs start at rest)
        base[(size_t) (cx::kSnap + k)] = 0.0f;
    }
    // MOTION as that module has it (one that says nothing of it: off)
    for (int i = cx::kMot; i < cx::kSnap; ++i)
    {
        const auto& c = engine.control (i);
        const float v = m.motion.isObject() && m.motion.hasProperty (juce::Identifier (c.id)) ? juce::jlimit (0.0f, 1.0f, (float) m.motion.getProperty (juce::Identifier (c.id), c.def)) : c.def;
        engine.set (i, v); base[(size_t) i] = engine.target (i);
    }
    moduleAt = m.id;
    baseId = 0;
    applyMorph (0.0);
    lawsSig = 0; motionLaws();
    touch();
    if (onChange) onChange();
    logAction ("module", 0, { { "module", m.id }, { "name", m.name } });
    return true;
}

void Session::moduleRename (int index, const juce::String& name)
{
    if (index < 0 || index >= (int) modules.size() || name.trim().isEmpty()) return;
    modules[(size_t) index].name = moduleNamed (name, modules, index);
    touch();
    if (onChange) onChange();
}

bool Session::moduleRemove (int index)
{
    if (index < 0 || index >= (int) modules.size() || index == moduleIndex()) return false;
    modules.erase (modules.begin() + index);
    touch();
    if (onChange) onChange();
    return true;
}

juce::var Session::moduleKnob (int module, int k) const
{
    if (module < 0 || module >= (int) modules.size() || k < 0 || k >= 16) return {};
    const var kn = module == moduleIndex() ? moduleKnobsNow() : modules[(size_t) module].knobs;
    return kn.isArray() && k < kn.size() ? kn[k] : var();
}

// ---- a pile, wherever it is kept
bool Session::pileIs (const PileAt& p) const
{
    if (p.what == 1) return p.index >= 0 && p.index < (int) stacks.size();
    if (p.what == 2) return p.index >= 0 && p.index < (int) keptKnobs.size() && keptKnobs[(size_t) p.index].stack;
    if (p.what == 3) return p.index >= 0 && p.index < (int) modules.size() && isStack (p.knob);
    return false;
}

bool Session::pileCan (const PileAt& p) const { return pileIs (p) && (p.what != 1 || rackCanChange (p.index)); }

// (a knob kept, or a knob of a module that is not in the chassis: read from how it is kept, changed, and kept again)
static bool pileKnobKept (const Session& s, const Session::PileAt& p) { return p.what == 2 || (p.what == 3 && p.index != s.moduleIndex()); }

juce::String Session::pileName (const PileAt& p) const
{
    if (! pileIs (p)) return {};
    if (p.what == 1) return stacks[(size_t) p.index].name;
    if (p.what == 2) return keptKnobs[(size_t) p.index].name();
    const MacroKnob kn = p.index == moduleIndex() ? knobs[(size_t) p.knob] : knobFrom (moduleKnob (p.index, p.knob));
    return kn.name.isNotEmpty() ? kn.name : "KNOB " + juce::String (p.knob + 1);
}

std::vector<juce::int64> Session::pileSnaps (const PileAt& p) const
{
    if (! pileIs (p)) return {};
    if (p.what == 1) return stacks[(size_t) p.index].snaps;
    if (p.what == 3 && p.index == moduleIndex()) return knobs[(size_t) p.knob].snaps;
    return knobFrom (p.what == 2 ? keptKnobs[(size_t) p.index].knob : moduleKnob (p.index, p.knob)).snaps;
}

juce::uint32 Session::pileKeysAt (const PileAt& p, int i) const
{
    if (! pileIs (p) || i < 0) return kAllLanes;
    if (p.what == 1) return i < (int) stacks[(size_t) p.index].snaps.size() ? stacks[(size_t) p.index].keysAt ((size_t) i) : kAllLanes;
    const MacroKnob kn = p.what == 3 && p.index == moduleIndex() ? knobs[(size_t) p.knob] : knobFrom (p.what == 2 ? keptKnobs[(size_t) p.index].knob : moduleKnob (p.index, p.knob));
    return i < (int) kn.snaps.size() ? kn.keysAt ((size_t) i) : kAllLanes;
}

// (what is done to a knob as it is kept: the knob read, changed, and kept again in its place)
static bool pileKept (Session& s, const Session::PileAt& p, const std::function<bool (MacroKnob&)>& change)
{
    var& held = p.what == 2 ? s.keptKnobs[(size_t) p.index].knob : s.modules[(size_t) p.index].knobs;
    MacroKnob kn = s.knobFrom (p.what == 2 ? held : (held.isArray() && p.knob < held.size() ? var (held[p.knob]) : var()));
    if (! change (kn)) return false;
    if (p.what == 2) held = s.knobVar (kn);
    else if (auto* a = held.getArray(); a != nullptr && p.knob < a->size()) a->set (p.knob, s.knobVar (kn));
    else return false;
    s.touch();
    if (s.onChange) s.onChange();
    return true;
}

bool Session::pileInsert (const PileAt& p, int at, juce::int64 id, juce::uint32 keys)
{
    if (! pileCan (p) || findSnap (id) == nullptr) return false;
    if (p.what == 1) return rackInsert (p.index, at, id, keys);
    if (! pileKnobKept (*this, p)) { const int n = (int) knobs[(size_t) p.knob].snaps.size(); return stackInsert (p.knob, at < 0 ? n : at, id, keys); }
    return pileKept (*this, p, [&] (MacroKnob& kn)
    {
        if ((int) kn.snaps.size() >= kStackMost) return false;
        kn.add (id, keysOf (keys));
        const int last = (int) kn.snaps.size() - 1;
        if (at >= 0) for (int i = last; i > juce::jlimit (0, last, at); --i) kn.swapAt ((size_t) i, (size_t) i - 1);
        return true;
    });
}

void Session::pileMove (const PileAt& p, int from, int to)
{
    if (! pileCan (p)) return;
    if (p.what == 1) { rackMove (p.index, from, to); return; }
    if (! pileKnobKept (*this, p)) { stackMove (p.knob, from, to); return; }
    pileKept (*this, p, [&] (MacroKnob& kn)
    {
        const int n = (int) kn.snaps.size();
        if (from < 0 || from >= n) return false;
        const int goal = juce::jlimit (0, n - 1, to);
        if (goal == from) return false;
        for (int i = from; i < goal; ++i) kn.swapAt ((size_t) i, (size_t) i + 1);
        for (int i = from; i > goal; --i) kn.swapAt ((size_t) i, (size_t) i - 1);
        return true;
    });
}

void Session::pileDrop (const PileAt& p, int i)
{
    if (! pileCan (p)) return;
    if (p.what == 1) { rackDrop (p.index, i); return; }
    if (! pileKnobKept (*this, p)) { stackDrop (p.knob, i); return; }
    pileKept (*this, p, [&] (MacroKnob& kn) { if (i < 0 || i >= (int) kn.snaps.size()) return false; kn.dropAt ((size_t) i); return true; });
}

void Session::pileKeys (const PileAt& p, int i, juce::uint32 keys)
{
    if (! pileCan (p)) return;
    if (p.what == 1) { rackLanes (p.index, i, keys); return; }
    if (! pileKnobKept (*this, p)) { stackLanes (p.knob, i, keys); return; }
    pileKept (*this, p, [&] (MacroKnob& kn) { if (i < 0 || i >= (int) kn.snaps.size()) return false; kn.setLanes ((size_t) i, keysOf (keys)); return true; });
}

// ---- a macro knob's ties, wherever it is kept
bool Session::tieIs (const PileAt& p) const
{
    if (p.what == 2) return p.index >= 0 && p.index < (int) keptKnobs.size() && ! keptKnobs[(size_t) p.index].stack;
    if (p.what == 3) return p.index >= 0 && p.index < (int) modules.size() && p.knob >= 0 && p.knob < 16 && ! isStack (p.knob) && ! isCorner (p.knob);
    return false;
}

std::vector<MacroKnob::Link> Session::ties (const PileAt& p) const
{
    if (! tieIs (p)) return {};
    if (p.what == 3 && p.index == moduleIndex()) return knobs[(size_t) p.knob].links;
    return knobFrom (p.what == 2 ? keptKnobs[(size_t) p.index].knob : moduleKnob (p.index, p.knob)).links;
}

bool Session::tieChange (const PileAt& p, const std::function<bool (MacroKnob&)>& change)
{
    if (! tieIs (p)) return false;
    if (pileKnobKept (*this, p)) return pileKept (*this, p, change);
    // (the knob in the chassis: what it is adding to the sound follows, and so does what its MOTION reaches)
    if (learning == p.knob) return false;
    if (! change (knobs[(size_t) p.knob])) return false;
    applyMorph (glideBeats());
    lawsSig = 0; motionLaws();
    touch();
    if (onChange) onChange();
    return true;
}

bool Session::tieSet (const PileAt& p, int i, int hundredths)
{
    return tieChange (p, [&] (MacroKnob& kn)
    {
        if (i < 0 || i >= (int) kn.links.size()) return false;
        auto& l = kn.links[(size_t) i];
        int h = juce::jlimit (-100, 100, hundredths);
        if (h == 0) h = l.amount < 0.0f ? -1 : 1;
        const float a = (float) h / 100.0f;
        if (std::abs (a - l.amount) < 0.0005f) return false;
        l.amount = a;
        return true;
    });
}

bool Session::tieDrop (const PileAt& p, int i)
{
    return tieChange (p, [&] (MacroKnob& kn)
    {
        if (i < 0 || i >= (int) kn.links.size()) return false;
        kn.links.erase (kn.links.begin() + i);
        return true;
    });
}

int Session::stacksHolding (juce::int64 id, juce::StringArray* names) const
{
    int n = 0;
    for (const auto& st : stacks)
        if (std::find (st.snaps.begin(), st.snaps.end(), id) != st.snaps.end()) { ++n; if (names != nullptr) names->add (st.name); }
    return n;
}

juce::String Session::moduleSays (int index) const
{
    if (index < 0 || index >= (int) modules.size()) return {};
    const var kn = index == moduleIndex() ? moduleKnobsNow() : modules[(size_t) index].knobs;
    juce::StringArray macro, stack;
    for (int k = 0; k < 16 && kn.isArray() && k < kn.size(); ++k)
    {
        if (isCorner (k)) continue;
        const var o = kn[k];
        const bool holds = isStack (k) ? o.getProperty ("snaps", var()).size() > 0 : o.getProperty ("links", var()).size() > 0;
        if (! holds) continue;
        const auto nm = o.getProperty ("name", "").toString();
        (isStack (k) ? stack : macro).add (nm.isNotEmpty() ? nm : "KNOB " + juce::String (k + 1));
    }
    juce::String t;
    t << macro.size() << (macro.size() == 1 ? " MACRO KNOB" : " MACRO KNOBS") << (macro.isEmpty() ? "" : ": " + macro.joinIntoString (", "))
      << ".  " << stack.size() << (stack.size() == 1 ? " STACK KNOB" : " STACK KNOBS") << (stack.isEmpty() ? "" : ": " + stack.joinIntoString (", "));
    return t;
}

// ---- the rack and the web library
static juce::Array<var> offOf (juce::uint32 lanes);
static juce::uint32 lanesFromOff (const var& off);
static juce::String rackName (const juce::String& n) { return n.trim().toUpperCase().substring (0, 40); }

juce::String Session::stackPrint (const Stack& st) const
{
    // (a song is the web library's to arrange: it is never changed here, so it is always as last agreed)
    if (st.kind == "song" && ! st.here) return st.mark;
    juce::String s;
    s << "stack|" << rackName (st.name) << "|";
    for (size_t i = 0; i < st.snaps.size(); ++i)
    {
        const auto* sn = findSnap (st.snaps[i]);
        s << (sn != nullptr && sn->uid.isNotEmpty() ? sn->uid : "#" + juce::String (st.snaps[i]));
        if (const auto m = st.lanesAt (i) & kAllLanes; m != kAllLanes) s << "/" << juce::String::toHexString ((int) m);
        s << ",";
    }
    return s;
}

juce::String Session::stackPrintOf (const var& there, const std::set<juce::String>& none) const
{
    const bool song = there.getProperty ("kind", "").toString() == "song";
    juce::String s;
    s << (song ? "song|" : "stack|") << rackName (there.getProperty ("name", "").toString()) << "|";
    if (auto* items = there.getProperty ("items", var()).getArray())
        for (auto& it : *items)
        {
            const auto u = it.getProperty ("u", "").toString();
            if (u.isEmpty() || none.count (u) != 0) continue;
            s << u;
            if (song) s << ":" << (int) it.getProperty ("bars", 8) << ":" << (int) it.getProperty ("morph", 0);
            else if (const auto m = lanesFromOff (it.getProperty ("off", var())) & kAllLanes; m != kAllLanes) s << "/" << juce::String::toHexString ((int) m);
            s << ",";
        }
    return s;
}

juce::var Session::stackBody (const Stack& st, bool& whole) const
{
    whole = true;
    juce::Array<var> items;
    for (size_t i = 0; i < st.snaps.size(); ++i)
    {
        const auto* sn = findSnap (st.snaps[i]);
        if (sn == nullptr || sn->uid.isEmpty()) { whole = false; continue; }
        auto* it = new juce::DynamicObject();
        it->setProperty ("u", sn->uid);
        if (const auto m = st.lanesAt (i) & kAllLanes; m != kAllLanes) it->setProperty ("off", offOf (m));
        items.add (var (it));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", st.name);
    o->setProperty ("items", items);
    o->setProperty ("load", true);          // (lit for the instrument: that is where it is)
    return var (o);
}

std::vector<Session::RackStep> Session::rackPlan (const var& stacksThere, const std::set<juce::String>& none, juce::StringArray& lacking) const
{
    std::vector<RackStep> plan;
    std::set<juce::String> uidsHere;
    for (auto& x : library) if (x.uid.isNotEmpty()) uidsHere.insert (x.uid);
    // each stack the library has, by id. For the rack: one of chips (a stack, a song), lit for the instrument, with
    // something in it. lacks: it names a snapshot that is not here yet (and that some library still has).
    struct There { var v; bool chips = false, lit = false, lacks = false; juce::String print; };
    std::map<juce::String, There> there;
    juce::StringArray order;
    if (auto* all = stacksThere.getArray())
        for (auto& v : *all)
        {
            There t;
            t.v = v;
            const auto id = v.getProperty ("id", "").toString(), kind = v.getProperty ("kind", "").toString();
            if (id.isEmpty()) continue;
            t.chips = kind == "stack" || kind == "song";
            int has = 0;
            if (auto* items = v.getProperty ("items", var()).getArray())
                for (auto& it : *items)
                {
                    const auto u = it.getProperty ("u", "").toString();
                    if (u.isEmpty() || none.count (u) != 0) continue;
                    if (uidsHere.count (u) != 0) ++has;
                    else if (t.chips && (bool) v.getProperty ("load", false)) { t.lacks = true; lacking.addIfNotAlreadyThere (u); }
                }
            t.lit = t.chips && (bool) v.getProperty ("load", false) && (has > 0 || t.lacks);
            t.print = stackPrintOf (v, none);
            there[id] = t;
            order.add (id);
        }
    auto goneAt = [this] (const juce::String& id) { for (int i = 0; i < stacksGone.size(); ++i) if (stacksGone[i].getProperty ("id", "").toString() == id) return i; return -1; };
    std::set<juce::String> idsHere;
    for (auto& st : stacks)
    {
        idsHere.insert (st.id);
        if (st.snaps.empty() && st.kind != "song") continue;        // (one with nothing in it is the instrument's alone until it holds something: the library would put it out of the rack)
        const auto f = there.find (st.id);
        const auto line = stackPrint (st);
        // changed here since the two agreed. (One an earlier build's LOAD brought, before lines were kept, counts as
        // unchanged: it is the library's, and the library's is taken. One made here that has never gone is always due.)
        const bool changed = st.here ? line != st.mark : (st.mark.isNotEmpty() && line != st.mark);
        bool whole = true;
        RackStep step;
        step.id = st.id; step.was = line; step.mark = line;
        step.body = stackBody (st, whole);
        if (f == there.end())
        {
            // the library has no such stack: it never had it, or it was removed there
            if (st.here && st.mark.isEmpty()) { if (whole) { step.net = RackStep::post; step.here = RackStep::setMark; plan.push_back (step); } }
            else if (! changed) { step.here = RackStep::drop; plan.push_back (step); }
            else if (whole) { step.net = RackStep::restore; step.here = RackStep::setMark; plan.push_back (step); }     // (removed there, changed here since: the instrument's stands)
            continue;
        }
        const auto& t = f->second;
        if (! t.chips || t.lacks) continue;         // (not the rack's; or not all of it is here yet)
        if (! t.lit)
        {
            // unlit for the instrument there, or nothing left in it: it leaves the rack here, unless it was changed here since
            if (! changed) { step.here = RackStep::drop; plan.push_back (step); }
            else if (whole) { step.net = RackStep::patch; step.here = RackStep::setMark; plan.push_back (step); }
            continue;
        }
        if (t.print == line) { if (st.mark != line) { step.here = RackStep::setMark; plan.push_back (step); } continue; }
        if (! changed) { step.here = RackStep::take; step.mark = t.print; step.there = t.v; step.body = var(); plan.push_back (step); }
        else if (whole)
        {
            // (the name goes only if it is another name: one given there in small letters is left as it was written)
            if (rackName (t.v.getProperty ("name", "").toString()) == rackName (st.name)) step.body.getDynamicObject()->removeProperty ("name");
            step.net = RackStep::patch; step.here = RackStep::setMark; plan.push_back (step);
        }
    }
    // those the library has for the instrument that are not here: new there, or put out of the rack here
    for (auto& id : order)
    {
        const auto& t = there[id];
        if (! t.lit || t.lacks || idsHere.count (id) != 0) continue;
        RackStep step;
        step.id = id;
        if (const int g = goneAt (id); g >= 0)
        {
            // put out here. If it is still as it was: out there too (one made here is removed there, where it can be put
            // back; one made there is only unlit for the instrument). Changed there since: it is not, and comes back.
            step.gone = true;
            if (t.print == stacksGone[g].getProperty ("mark", "").toString())
                step.net = (bool) stacksGone[g].getProperty ("here", false) ? RackStep::removeThere : RackStep::unlight;
            plan.push_back (step);
            continue;
        }
        step.here = RackStep::take; step.mark = t.print; step.there = t.v;
        plan.push_back (step);
    }
    // (put out here, and the library no longer has it for the instrument either: nothing left to say)
    for (int i = 0; i < stacksGone.size(); ++i)
    {
        const auto id = stacksGone[i].getProperty ("id", "").toString();
        const auto f = there.find (id);
        if (f == there.end() || ! f->second.lit) { RackStep step; step.id = id; step.gone = true; plan.push_back (step); }
    }
    return plan;
}

bool Session::rackDone (const RackStep& step)
{
    bool did = false;
    if (step.gone)
        for (int i = stacksGone.size(); --i >= 0;) if (stacksGone[i].getProperty ("id", "").toString() == step.id) { stacksGone.remove (i); did = true; }
    int at = -1;
    for (int i = 0; i < (int) stacks.size() && at < 0; ++i) if (stacks[(size_t) i].id == step.id) at = i;
    const bool asWas = at >= 0 && stackPrint (stacks[(size_t) at]) == step.was;
    bool ok = true;
    switch (step.here)
    {
        case RackStep::nothing: break;
        case RackStep::setMark:
            if (asWas) { if (stacks[(size_t) at].mark != step.mark) { stacks[(size_t) at].mark = step.mark; did = true; } } else ok = false;
            break;
        case RackStep::drop:
            if (asWas) { stacks.erase (stacks.begin() + at); did = true; } else ok = false;
            break;
        case RackStep::take:
        {
            if (at >= 0 && ! asWas) { ok = false; break; }
            std::map<juce::String, juce::int64> here;
            for (auto& x : library) if (x.uid.isNotEmpty()) here[x.uid] = x.id;
            Stack st;
            st.id = step.id;
            st.name = rackName (step.there.getProperty ("name", "").toString());
            st.kind = step.there.getProperty ("kind", "stack").toString() == "song" ? "song" : "stack";
            st.here = at >= 0 ? stacks[(size_t) at].here : step.there.getProperty ("extra", var()).getProperty ("from", "").toString() == "sf000";
            juce::Array<var> parts;
            bool all = true;
            if (auto* items = step.there.getProperty ("items", var()).getArray())
                for (auto& it : *items)
                    if (const auto f = here.find (it.getProperty ("u", "").toString()); f != here.end())
                    {
                        st.snaps.push_back (f->second);
                        const auto m = st.kind == "song" ? kAllLanes : (lanesFromOff (it.getProperty ("off", var())) & kAllLanes);
                        st.lanes.push_back (m);
                        all = all && m == kAllLanes;
                        if (st.kind == "song")
                        {
                            auto* o = new juce::DynamicObject();
                            o->setProperty ("uid", f->first); o->setProperty ("bars", it.getProperty ("bars", 8)); o->setProperty ("morph", it.getProperty ("morph", 0));
                            parts.add (var (o));
                        }
                    }
            if (all) st.lanes.clear();
            if (st.snaps.empty()) { ok = false; break; }
            // (the web library says nothing of a snapshot's N: what was lit here stays lit, on the snapshots still in it)
            if (at >= 0 && st.kind != "song")
                for (size_t i = 0; i < st.snaps.size(); ++i)
                    for (size_t j = 0; j < stacks[(size_t) at].snaps.size(); ++j)
                        if (stacks[(size_t) at].snaps[j] == st.snaps[i] && (stacks[(size_t) at].keysAt (j) & (kNotesKey | kBlendKey)) != 0)
                        { st.lanes.resize (st.snaps.size(), kAllLanes); st.lanes[i] |= stacks[(size_t) at].keysAt (j) & (kNotesKey | kBlendKey); break; }
            auto* keep = new juce::DynamicObject();
            if (at >= 0) if (auto* was = stacks[(size_t) at].kept.getDynamicObject()) for (auto& q : was->getProperties()) keep->setProperty (q.name, q.value);
            if (st.kind == "song") keep->setProperty ("parts", parts);
            st.kept = var (keep);
            st.mark = step.mark;
            if (at >= 0) stacks[(size_t) at] = st; else stacks.push_back (st);
            did = true;
            break;
        }
    }
    if (did) { touch(); if (onChange) onChange(); }
    return ok;
}

void Session::rackOrder (const var& stacksThere)
{
    std::map<juce::String, int> place;
    if (auto* all = stacksThere.getArray())
        for (int i = 0; i < all->size(); ++i) place[all->getReference (i).getProperty ("id", "").toString()] = i;
    auto of = [&place] (const Stack& st) { const auto f = place.find (st.id); return f != place.end() ? f->second : std::numeric_limits<int>::max(); };
    bool inOrder = true;
    for (size_t i = 1; i < stacks.size(); ++i) inOrder = inOrder && of (stacks[i - 1]) <= of (stacks[i]);
    if (inOrder) return;
    std::stable_sort (stacks.begin(), stacks.end(), [&of] (const Stack& a, const Stack& b) { return of (a) < of (b); });
    touch();
    if (onChange) onChange();
}

bool Session::knobMove (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= 16 || to >= 16 || ! isStack (from) || ! isStack (to) || ! knobHolds (from) || knobHolds (to)) return false;
    knobs[(size_t) to] = knobs[(size_t) from];
    knobs[(size_t) from] = MacroKnob();
    const float at = engine.target (cx::kSnap + from);
    engine.set (cx::kSnap + to, at); engine.set (cx::kSnap + from, 0.0f);
    base[(size_t) (cx::kSnap + to)] = at; base[(size_t) (cx::kSnap + from)] = 0.0f;
    for (int u = 0; u < cx::kMotUnits; ++u)
        if ((int) std::lround (engine.target (cx::mot (u, 2)) * 15.0f) == from)
        {
            engine.set (cx::mot (u, 2), (float) to / 15.0f);
            base[(size_t) cx::mot (u, 2)] = engine.target (cx::mot (u, 2));
        }
    applyMorph (glideBeats());
    lawsSig = 0; motionLaws();
    touch();
    if (onChange) onChange();
    return true;
}

// The parts of the rig a snapshot of a stack does not take, as the web library names them: a lane by its slug, then
// "returns", "bus", "master". (What is not named is taken: a lane a later rig adds comes with the rest.)
static juce::Array<var> offOf (juce::uint32 lanes)
{
    juce::Array<var> off;
    for (int l = 0; l < kLanes; ++l) if (((lanes >> l) & 1u) == 0) off.add (juce::String (sf0::lanes()[(size_t) l].slug));
    if (((lanes >> kLanes) & 1u) == 0) off.add ("returns");
    if (((lanes >> (kLanes + 1)) & 1u) == 0) off.add ("bus");
    if (((lanes >> (kLanes + 2)) & 1u) == 0) off.add ("master");
    return off;
}
static juce::uint32 lanesFromOff (const var& off)
{
    juce::uint32 m = Session::kAllLanes;
    if (auto* a = off.getArray())
        for (auto& o : *a)
        {
            const auto key = o.toString();
            if (key == "returns") m &= ~(1u << kLanes);
            else if (key == "bus") m &= ~(1u << (kLanes + 1));
            else if (key == "master") m &= ~(1u << (kLanes + 2));
            else for (int l = 0; l < kLanes; ++l) if (key == sf0::lanes()[(size_t) l].slug) m &= ~(1u << l);
        }
    return m;
}

juce::String Session::knobPrint (int k) const
{
    const auto& kn = knobs[(size_t) k];
    juce::String s;
    s << (isStack (k) ? "stack" : "learn") << "|" << kn.name << "|" << juce::String (isStack (k) ? 0.0f : kn.depth, 3) << "|";
    if (isStack (k))
        for (size_t i = 0; i < kn.snaps.size(); ++i)
        {
            const auto* sn = findSnap (kn.snaps[i]);
            s << (sn != nullptr && sn->uid.isNotEmpty() ? sn->uid : juce::String (kn.snaps[i]));
            if (const auto m = kn.lanesAt (i) & kAllLanes; m != kAllLanes) s << "/" << juce::String::toHexString ((int) m);
            s << ",";
        }
    s << "|";
    if (! isStack (k)) for (const auto& l : kn.links) s << engine.control (l.ctl).id << ":" << juce::String (l.amount, 3) << ",";
    return s;
}

juce::var Session::knobBody (int k, bool& whole) const
{
    const auto& kn = knobs[(size_t) k];
    whole = true;
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", kn.name.isNotEmpty() ? kn.name : "KNOB " + juce::String (k + 1));
    o->setProperty ("kind", isStack (k) ? "stack" : "learn");
    juce::Array<var> items;
    if (isStack (k))
        for (size_t i = 0; i < kn.snaps.size(); ++i)
        {
            const auto* sn = findSnap (kn.snaps[i]);
            if (sn == nullptr || sn->uid.isEmpty()) { whole = false; continue; }
            auto* it = new juce::DynamicObject();
            it->setProperty ("u", sn->uid);
            if (const auto m = kn.lanesAt (i) & kAllLanes; m != kAllLanes) it->setProperty ("off", offOf (m));
            items.add (var (it));
        }
    o->setProperty ("items", items);
    auto* ex = new juce::DynamicObject();
    if (! isStack (k))
    {
        o->setProperty ("depth", kn.depth);
        juce::Array<var> ls;
        for (const auto& l : kn.links)
        {
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("c", engine.control (l.ctl).id); lo->setProperty ("d", l.amount);
            ls.add (var (lo));
        }
        for (auto& v : kn.linksKept) ls.add (v);
        if (ls.size() > 0) ex->setProperty ("links", ls);
    }
    ex->setProperty ("from", "sf000");
    o->setProperty ("extra", var (ex));
    return var (o);
}

// A module of the chassis as the knob in that slot would hold it. A stack slot (5 to 12, 14, 15): its snapshots that are here,
// each with the parts of the rig it takes. A macro slot (1 to 4): the knobs of this rig it is tied to. A module of the
// other kind for its slot holds nothing a knob there could take (wrong: so the slot is left, and said).
struct ModuleHere { bool stack = false, wrong = false; std::vector<juce::int64> sn; std::vector<juce::uint32> lanes; std::vector<MacroKnob::Link> ln; juce::Array<var> lnKept; juce::String name; float depth = -1.0f; };
static ModuleHere moduleHere (const var& v, int slot, const Engine& engine, const std::map<juce::String, juce::int64>& here)
{
    ModuleHere m;
    if (Session::isCorner (slot)) { m.wrong = true; return m; }       // (13 and 16 hold no knob)
    m.stack = Session::isStack (slot);
    const auto kind = v.getProperty ("kind", "stack").toString();
    if (m.stack)
    {
        // (a library from before the two kinds may call a knob's stack of snapshots a "macro": it is a stack)
        const var off = v.getProperty ("off", var());
        if (auto* a = v.getProperty ("uids", var()).getArray())
            for (int i = 0; i < a->size() && (int) m.sn.size() < Session::kStackMost; ++i)
                if (const auto f = here.find ((*a)[i].toString()); f != here.end())
                {
                    m.sn.push_back (f->second);
                    m.lanes.push_back (off.isArray() && i < off.size() ? lanesFromOff (off[i]) : Session::kAllLanes);
                }
        m.wrong = kind == "learn";
        bool all = true;
        for (auto l : m.lanes) all = all && l == Session::kAllLanes;
        if (all) m.lanes.clear();
    }
    else
    {
        if (auto* ls = v.getProperty ("links", var()).getArray())
            for (auto& l : *ls)
            {
                const int i = engine.find (l.getProperty ("c", "").toString());
                const float d = juce::jlimit (-1.0f, 1.0f, (float) (double) l.getProperty ("d", 0.0));
                if (Session::reaches (engine, i)) { if (std::abs (d) >= 0.004f) m.ln.push_back ({ i, d }); }
                else m.lnKept.add (l);                                  // (a knob this rig does not have: carried, as a file's is)
            }
        std::sort (m.ln.begin(), m.ln.end(), [] (const MacroKnob::Link& x, const MacroKnob::Link& y) { return x.ctl < y.ctl; });
        m.wrong = m.ln.empty() && m.lnKept.isEmpty();
        if (const var d = v.getProperty ("depth", var()); d.isDouble() || d.isInt()) m.depth = juce::jlimit (0.0f, 2.0f, (float) d);
    }
    m.name = v.getProperty ("name", "").toString().trim().toUpperCase().substring (0, 16);
    return m;
}
static bool holdsThat (const MacroKnob& knob, const ModuleHere& m)
{
    if (m.stack)
    {
        bool same = knob.snaps == m.sn;
        for (size_t i = 0; same && i < m.sn.size(); ++i) same = (knob.lanesAt (i) & Session::kAllLanes) == (i < m.lanes.size() ? m.lanes[i] : Session::kAllLanes);
        return same;
    }
    bool same = knob.links.size() == m.ln.size();
    for (size_t i = 0; same && i < m.ln.size(); ++i) same = knob.links[i].ctl == m.ln[i].ctl && std::abs (knob.links[i].amount - m.ln[i].amount) < 1.0e-4f;
    return same;
}

bool Session::moduleFits (int k, const var& module)
{
    const auto kind = module.getProperty ("kind", "stack").toString();
    if (isCorner (k)) return false;
    if (isStack (k)) return kind != "learn";
    if (auto* ls = module.getProperty ("links", var()).getArray()) return ! ls->isEmpty();
    return false;
}

// A knob kept aside (knobsBroughtOver), as the library takes a module: off the chassis.
juce::var Session::asideBody (int i, bool& whole) const
{
    whole = true;
    if (i < 0 || i >= knobsAside.size()) return {};
    const var a = knobsAside[i];
    const bool stack = a.getProperty ("kind", "stack").toString() != "learn";
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", a.getProperty ("name", "").toString().isNotEmpty() ? a.getProperty ("name", "").toString() : juce::String ("FROM BEFORE"));
    o->setProperty ("kind", stack ? "stack" : "learn");
    juce::Array<var> items;
    const var ln = a.getProperty ("lanes", var());
    if (auto* ids = a.getProperty ("snaps", var()).getArray())
        for (int n = 0; stack && n < ids->size(); ++n)
        {
            const auto* sn = findSnap ((juce::int64) (*ids)[n]);
            if (sn == nullptr) continue;
            if (sn->uid.isEmpty()) { whole = false; continue; }
            auto* it = new juce::DynamicObject();
            it->setProperty ("u", sn->uid);
            if (const auto m = ln.isArray() && n < ln.size() ? ((juce::uint32) (int) ln[n] & kAllLanes) : kAllLanes; m != kAllLanes) it->setProperty ("off", offOf (m));
            items.add (var (it));
        }
    o->setProperty ("items", items);
    auto* ex = new juce::DynamicObject();
    if (! stack)
    {
        o->setProperty ("depth", a.getProperty ("depth", 1.0));
        ex->setProperty ("links", a.getProperty ("links", var()));
    }
    ex->setProperty ("from", "sf000");
    o->setProperty ("extra", var (ex));
    return var (o);
}

bool Session::knobIs (int k, const var& module) const
{
    std::map<juce::String, juce::int64> here;
    for (auto& s : library) if (s.uid.isNotEmpty()) here[s.uid] = s.id;
    const auto m = moduleHere (module, k, engine, here);
    const auto& knob = knobs[(size_t) k];
    return ! m.wrong && holdsThat (knob, m) && knob.name == m.name && (m.stack || m.depth < 0.0f || std::abs (knob.depth - m.depth) < 1.0e-4f);
}

juce::String Session::takeTray (const var& tray, const std::array<KnobPlan, 16>* plan)
{
    std::map<juce::String, juce::int64> here;
    for (auto& s : library) if (s.uid.isNotEmpty()) here[s.uid] = s.id;
    auto idsOf = [&here] (const var& uids)
    {
        std::vector<juce::int64> out;
        if (auto* a = uids.getArray())
            for (auto& u : *a) if (const auto f = here.find (u.toString()); f != here.end()) out.push_back (f->second);
        return out;
    };
    // a chip named there since it was last here
    int renamed = 0;
    if (auto* chips = tray.getProperty ("chips", var()).getDynamicObject())
        for (auto& c : chips->getProperties())
            if (const auto f = here.find (c.name.toString()); f != here.end())
                if (const auto* s = findSnap (f->second))
                    if (const auto nm = nameHere (c.value.getProperty ("name", "").toString()); nm.isNotEmpty() && nm != s->name) { rename (f->second, nm); ++renamed; }
    // The stacks are not the tray's to say any more: the rack is kept the same as the library's, each way, by ONLINE
    // (rackPlan), and LOAD has it done before it comes here. The tray's first song is still the arrangement's.
    var song;
    if (auto* sv = tray.getProperty ("stacks", var()).getArray())
        for (auto& v : *sv)
            if (v.getProperty ("kind", "stack").toString() == "song" && song.isVoid() && ! idsOf (v.getProperty ("uids", var())).empty()) song = v;
    // the knobs. With no plan (nobody has seen to what they hold): each module onto its knob if the knob holds nothing. With
    // one: the chassis says what each knob holds, and a knob it may take is taken whatever it held (Online has sent that up).
    juce::StringArray placed, left, changed, cleared, wrongKind;
    std::array<bool, 16> inPocket {};
    if (auto* kn = tray.getProperty ("knobs", var()).getArray())
        for (auto& v : *kn)
        {
            const int k = (int) v.getProperty ("slot", 0) - 1;
            if (k < 0 || k >= 16) continue;
            const auto m = moduleHere (v, k, engine, here);
            if (m.wrong) { wrongKind.add (juce::String (k + 1)); inPocket[(size_t) k] = true; continue; }       // (not a knob's to take: what the knob holds is left)
            if (m.sn.empty() && m.ln.empty()) continue;
            inPocket[(size_t) k] = true;
            auto& knob = knobs[(size_t) k];
            const bool may = plan != nullptr && (*plan)[(size_t) k].what == KnobPlan::take;
            const bool same = holdsThat (knob, m), held = knobHolds (k);
            if (! same)
            {
                if (held && ! may) { left.add (juce::String (k + 1)); continue; }
                if (learning == k) learnDrop();
                const auto on = knob.picked;
                // (the web library says nothing of a snapshot's N: what was lit here stays lit, on the snapshots still on the knob)
std::vector<std::pair<juce::int64, juce::uint32>> lit;       // (and nothing of its M: the same)
                for (size_t i = 0; i < knob.snaps.size(); ++i) if (const auto own = knob.keysAt (i) & (kNotesKey | kBlendKey); own != 0) lit.push_back ({ knob.snaps[i], own });
                knob.snaps = m.sn; knob.lanes = m.lanes; knob.picked = 0;
                for (size_t i = 0; i < knob.snaps.size(); ++i)
                    for (auto& was : lit) if (was.first == knob.snaps[i]) { knob.setLanes (i, (knob.keysAt (i) & kAllLanes) | was.second); break; }
                knob.links = m.ln; knob.linksKept = m.lnKept;
                // (a stack knob that stood on a snapshot the new stack holds too goes to that one's place in it; else it stays where it is)
                if (m.stack) { for (auto id : m.sn) if (id == on) knob.picked = on; }
            }
            if (! same || may)
            {
                knob.name = m.name;
                if (m.depth >= 0.0f) knob.depth = m.depth;
            }
            if (! same)
            {
                if (m.stack) stackSettle (k, knob.picked); else applyMorph (glideBeats());
                (held ? changed : placed).add (juce::String (k + 1));
            }
            if (plan != nullptr && may) { knob.more.set ("lib", v.getProperty ("id", "").toString()); knob.more.set ("libmark", knobPrint (k)); }
        }
    if (plan != nullptr)
        for (int k = 0; k < 16; ++k)
        {
            const auto& p = (*plan)[(size_t) k];
            auto& knob = knobs[(size_t) k];
            if (p.what == KnobPlan::clear && ! inPocket[(size_t) k] && knobHolds (k))
            {
                // taken off the chassis there: it comes off here (the library has it)
                knobClear (k, false);            // (the chassis there already says so)
                cleared.add (juce::String (k + 1));
            }
            else if (p.what == KnobPlan::leave && p.lib.isNotEmpty()) { knob.more.set ("lib", p.lib); knob.more.set ("libmark", knobPrint (k)); }
        }
    // a song: the arrangement, if there is none
    int sung = 0; bool songLeft = false;
    if (auto* parts = song.getProperty ("parts", var()).getArray())
    {
        juce::Array<var> pv;
        for (auto& p : *parts)
            if (const auto f = here.find (p.getProperty ("uid", "").toString()); f != here.end())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("snap", f->second); o->setProperty ("bars", p.getProperty ("bars", 8)); o->setProperty ("morph", p.getProperty ("morph", 0));
                pv.add (var (o));
            }
        // (an arrangement that is this song already, part for part, is nothing to say anything about)
        bool isIt = (int) arrangement.size() == pv.size() && pv.size() > 0;
        for (int i = 0; isIt && i < pv.size(); ++i)
            isIt = arrangement[(size_t) i].snap == (juce::int64) pv[i]["snap"] && arrangement[(size_t) i].bars == juce::jlimit (1, kArrMaxBars, (int) pv[i]["bars"]);
        if (isIt) {}
        else if (! arrangement.empty()) songLeft = true;
        else
        {
            auto* a = new juce::DynamicObject();
            for (auto& m : arrMore) a->setProperty (m.name, m.value);
            a->setProperty ("parts", pv);
            arrFromVar (var (a));
            sung = (int) arrangement.size();
        }
    }
    touch();
    if (onLibrary) onLibrary();
    if (onChange) onChange();
    auto count = [] (int n, const char* one, const char* many) { return juce::String (n) + " " + (n == 1 ? one : many); };
    // (the knobs in their own order, whatever order the library listed its module in)
    for (auto* list : { &placed, &left, &changed, &cleared, &wrongKind })
        std::sort (list->begin(), list->end(), [] (const juce::String& x, const juce::String& y) { return x.getIntValue() < y.getIntValue(); });
    juce::String line = count ((int) stacks.size(), "STACK", "STACKS");
    if (placed.size() > 0) line << ", " << (placed.size() == 1 ? "KNOB " : "KNOBS ") << placed.joinIntoString (", ") << " FILLED";
    if (changed.size() > 0) line << ", " << (changed.size() == 1 ? "KNOB " : "KNOBS ") << changed.joinIntoString (", ") << " CHANGED (WHAT " << (changed.size() == 1 ? "IT" : "THEY") << " HELD IS IN THE LIBRARY)";
    if (cleared.size() > 0) line << ", " << (cleared.size() == 1 ? "KNOB " : "KNOBS ") << cleared.joinIntoString (", ") << " EMPTIED (OFF THE CHASSIS; IN THE LIBRARY)";
    if (wrongKind.size() > 0) line << ". " << (wrongKind.size() == 1 ? "KNOB " : "KNOBS ") << wrongKind.joinIntoString (", ") << (wrongKind.size() == 1 ? " WAS" : " WERE")
                                   << " LEFT: THE MODULE THERE HAS THE OTHER KIND IN THAT PLACE (1 TO 4 ARE MACRO KNOBS; 5 TO 12, 14 AND 15 STACK KNOBS; 13 AND 16 HOLD NONE)";
    if (left.size() > 0 && plan == nullptr) line << ". " << (left.size() == 1 ? "KNOB " : "KNOBS ") << left.joinIntoString (", ") << (left.size() == 1 ? " HOLDS" : " HOLD") << " SOMETHING AND " << (left.size() == 1 ? "WAS" : "WERE") << " LEFT: CLEAR IT AND LOAD AGAIN";
    if (sung > 0) line << ". THE SONG IS THE ARRANGEMENT (" << count (sung, "PART", "PARTS") << ")";
    if (songLeft) line << ". THE ARRANGEMENT WAS LEFT AS IT IS (CLEAR IT TO TAKE THE SONG)";
    if (renamed > 0) line << ". " << juce::String (renamed) << " RENAMED";
    return line;
}

// A snapshot written before the seventy-ninth build, made to mean on this one what it meant on that. Two things moved:
//   - the lists of models grew (GRANULAR on BASS and on LEAD and CHORD, the new kick on KICK), and a model is kept as
//     its place on its list, of 0 to 1: its place is found on the list of before and put on the list of now. (The lists
//     had grown once already, BASS from 8 and LEAD from 10, told then by where a value fell: that is done first.)
//   - GRANULAR has sound knobs of its own (Rig.h, ModelKnobs). On PAD, where it already was, the places held DETUNE,
//     COLOR, RES, POCKET DEPTH, DECAY and ATTACK: those go into the lane's CONFIG as they stood, and the places become
//     POSITION at the start of the source, SCAN at the pace the grains drifted at, SIZE from COLOR, four GRAINS at once,
//     SPRAY and PITCH from DETUNE: what GRANULAR did with those two knobs, so it sounds as it did.
static void fromListsBefore (Snapshot& s, const Engine& e, const std::array<int, kLanes>& taken)
{
    static std::array<int, kLanes> counts {};       // (how many models each lane has now: the same for as long as this runs)
    if (counts[0] == 0)
        for (int l = 0; l < kLanes; ++l)
        {
            counts[(size_t) l] = 1;
            for (auto& sp : e.setupParams ({ 0, l, 0 })) if (sp.id == "model") counts[(size_t) l] = std::max (1, sp.choices.size());
        }
    for (int l = 0; l < kLanes; ++l)
    {
        const juce::String module (lanes()[(size_t) l].module);
        if (taken[(size_t) l] >= Session::kListsNow) continue;         // (this lane is of now)
        const int before = module == "BASS" ? 10 : module == "LEAD" ? 12 : module == "KICK" ? 7 : module == "PAD" ? 9 : 0, now = counts[(size_t) l];
        // (what this build added, where it changes nothing: the lane is not left with what the snapshot before it set)
        if (s.hasSetup)
            for (auto& d : e.newerOff ({ 0, l, 0 }))
            {
                bool named = false;
                for (auto& kv : s.setup[(size_t) l]) named = named || kv.first == d.first;
                if (! named) s.setup[(size_t) l].push_back (d);
            }
        if (before == 0) continue;
        int model = -1;
        for (auto& kv : s.setup[(size_t) l])
        {
            if (kv.first != "model" && kv.first != "layer_model") continue;
            float v = kv.second;
            if (module == "BASS" || module == "LEAD")
            {
                // (the growth before this one, as Engine's grownPlace told it while the lists were that long)
                const float was = v * (float) (before - 2);
                const int k = (int) std::lround (was);
                if (std::abs (was - (float) k) <= 0.02f && k > 0 && k < before - 2) v = (float) k / (float) (before - 1);
            }
            const int at = juce::jlimit (0, before - 1, (int) std::lround (v * (float) (before - 1)));
            kv.second = (float) at / (float) std::max (1, now - 1);
            if (kv.first == "model") model = at;
        }
        if (module == "PAD" && model == 3 && s.hasSetup)
        {
            auto at = [&] (int k) -> float& { return s.values[(size_t) cx::lane (l, k)]; };
            auto put = [&] (const char* id, float v)
            {
                if (v < 0.0f) return;
                for (auto& kv : s.setup[(size_t) l]) if (kv.first == id) { kv.second = v; return; }
                s.setup[(size_t) l].push_back ({ id, v });
            };
            const float det = at (0) >= 0.0f ? at (0) : 0.5f, color = at (1) >= 0.0f ? at (1) : 0.5f;
            put ("detune", at (0)); put ("color", at (1)); put ("res", at (2)); put ("pocket_depth", at (3)); put ("decay", at (4)); put ("attack", at (5));
            at (0) = 0.0f;                                                              // POSITION
            at (1) = 0.5f + 0.5f * 0.1581139f;                                          // SCAN: a twentieth of the source's pace
            at (2) = std::log ((30.0f + 250.0f * color) / 5.0f) / std::log (100.0f);    // SIZE: 30 to 280 ms, as COLOR had it
            at (3) = 0.75f;                                                             // GRAINS: four at once
            at (4) = std::sqrt (0.3f * det);                                            // SPRAY: three tenths of the source at DETUNE's top
            at (5) = 0.5f * det;                                                        // PITCH: a hundredth either way at DETUNE's top
        }
    }
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
    {
        // which lists it was taken on: one figure for all of it, or (a chip the analysis made of lanes from snapshots of
        // different builds) a figure a lane, `listsBy`
        std::array<int, kLanes> taken;
        taken.fill ((int) v.getProperty ("lists", 1));
        if (auto* by = v.getProperty ("listsBy", var()).getArray())
            for (int l = 0; l < kLanes && l < by->size(); ++l) if ((*by)[l].isInt() || (*by)[l].isInt64()) taken[(size_t) l] = (int) (*by)[l];
        bool any = false;
        for (int t : taken) any = any || t < Session::kListsNow;
        if (any) fromListsBefore (s, e, taken);
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
var Session::stateVar (bool withChips)
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
    root->setProperty ("transition", transToVar());
    root->setProperty ("finish", finishPct());
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
    if (withChips) for (auto& s : library) lib.add (snapToVar (s, engine));
    for (auto& k : knobs) kn.add (knobVar (k));
    if (withChips) root->setProperty ("library", lib);
    root->setProperty ("knobs", kn);
    {
        // the modules kept: the one in the chassis as its knobs stand now, the others as they were left
        juce::Array<var> mv;
        for (auto& m : modules)
        {
            const bool here = m.id == moduleAt;
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", m.id); o->setProperty ("name", m.name);
            o->setProperty ("knobs", here ? moduleKnobsNow() : m.knobs);
            o->setProperty ("motion", here ? moduleMotionNow() : m.motion);
            mv.add (var (o));
        }
        if (mv.size() > 0) { root->setProperty ("modules", mv); root->setProperty ("moduleAt", moduleAt); }
    }
    if (! keptKnobs.empty())
    {
        juce::Array<var> kv;
        for (auto& kk : keptKnobs)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", kk.id); o->setProperty ("kind", kk.stack ? "stack" : "macro"); o->setProperty ("knob", kk.knob);
            kv.add (var (o));
        }
        root->setProperty ("keptKnobs", kv);
    }
    root->setProperty ("module", 3);                // (the snapshot module's two kinds: knobs 1 to 4 macro; 5 to 12, 14 and 15 stack; 13 and 16 none. 2: 13 and 16 were stack knobs too)
    if (stackCue) root->setProperty ("stackCue", true);
    if (knobsAside.size() > 0) root->setProperty ("knobsAside", knobsAside);
    if (knobsUnseated.size() > 0) root->setProperty ("knobsUnseated", knobsUnseated);
    if (! arrangement.empty() || arrMore.size() > 0) root->setProperty ("arrangement", arrToVar());
    if (arrs.size() > 1 || (arrs.size() == 1 && arrs[0].name != "ARRANGEMENT 1"))
    {
        // the arrangements kept: the one on the page as it stands now (it is "arrangement" too, for a build from before
        // these), the others as they were left. (One alone that was never named is not written: it is "arrangement".)
        juce::Array<var> av;
        for (auto& a : arrs)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", a.id); o->setProperty ("name", a.name);
            o->setProperty ("arrangement", a.id == arrAtId ? arrToVar() : a.parts);
            av.add (var (o));
        }
        root->setProperty ("arrangements", av); root->setProperty ("arrangementAt", arrAtId);
    }
    if (! stacks.empty())
    {
        juce::Array<var> sv;
        for (auto& st : stacks)
        {
            auto* o = new juce::DynamicObject();
            if (auto* was = st.kept.getDynamicObject()) for (auto& q : was->getProperties()) o->setProperty (q.name, q.value);      // (what this build does not know of it: carried)
            o->setProperty ("id", st.id); o->setProperty ("name", st.name); o->setProperty ("kind", st.kind);
            juce::Array<var> ids;
            for (auto id : st.snaps) ids.add (id);
            o->setProperty ("snaps", ids);
            if (st.here) o->setProperty ("here", true); else o->removeProperty ("here");
            if (st.mark.isNotEmpty()) o->setProperty ("mark", st.mark); else o->removeProperty ("mark");
            if (! st.lanes.empty()) { juce::Array<var> ln; for (size_t i = 0; i < st.snaps.size(); ++i) ln.add ((int) st.keysAt (i)); o->setProperty ("lanes", ln); }
            else o->removeProperty ("lanes");
            sv.add (var (o));
        }
        root->setProperty ("stacks", sv);
    }
    if (stacksGone.size() > 0) root->setProperty ("stacksGone", stacksGone);
    // The definitions: every one the file has held, and this build's own. Never pruned: a snapshot
    // made long ago still finds what it was made with.
    if (withChips)
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
    // From before the snapshot module's two kinds of knob: its knobs are about to be brought over (knobsBroughtOver).
    // The file as it is goes aside first, and is not one of the twenty backups that come and go.
    if (root.hasProperty ("knobs") && (int) root.getProperty ("module", 0) < 2)
        if (const auto keep = f.getSiblingFile ("session-before-two-kinds.json"); ! keep.existsAsFile()) f.copyFileTo (keep);
    takeState (root);
    return true;
}

// A session from before the snapshot module had two kinds of knob. Any of its sixteen could hold snapshots (to sweep
// through, adding how far each was from the first on top of the scene; or to step through), knobs it was tied to, or
// both. Now knobs 1 to 4 are macro knobs and 5 to 12, 14 and 15 stack knobs (13 and 16 are no knob: Session.h):
//  - a knob that is where its kind now lives stays where it is;
//  - a knob with snapshots on a place that is no stack knob's goes to the first free stack knob; a knob tied to knobs on 5 to 16 goes to the
//    first free of 1 to 4 (the same rule the web library puts its own chassis right by, so the two still agree);
//  - a knob that had both is two knobs now: its snapshots a stack knob, its ties a macro knob;
//  - one that finds no free knob of its kind is kept aside (ONLINE sends it to the library, off the chassis);
//  - but a knob on 1 to 4 that still holds what the rig came with (INIT and one of its five pushes: ENERGY and the
//    rest) is a macro knob in its place, tied to what that push moves by as much as it moves it (a move smaller than
//    0.4% of a knob's turn is left out). That is what it did before, adding its push to wherever the rig is, and it
//    is what those knobs are: a stack knob would take the whole set to the push.
// MOTION goes with the knob it was on, and each knob's place on its turn goes with it. What the sweeping knobs were
// adding to the scene is returned, to be put into the scene itself: nothing sounds different for the change.
// twoKinds: the session is of the build that already had the two kinds, with stack knobs on 13 and 16 too. Those two
// are no knob now (Session.h): a stack there goes to the first free stack knob by the same rule, and nothing else moves.
std::vector<float> Session::knobsBroughtOver (const std::array<MacroKnob, 16>& asRead, const std::array<bool, 16>& stepped, bool twoKinds)
{
    auto read = asRead;
    for (int k = 0; k < kMacroKnobs && ! twoKinds; ++k)
    {
        auto& kn = read[(size_t) k];
        if (stepped[(size_t) k] || kn.snaps.size() != 2 || kn.snaps[0] != kInitId || kn.snaps[1] <= kInitId || kn.snaps[1] > kInitId + 5) continue;
        const auto* from = findSnap (kn.snaps[0]);
        const auto* to = findSnap (kn.snaps[1]);
        if (from == nullptr || to == nullptr) continue;
        for (int i = 0; i < kMorphEnd; ++i)
        {
            if (! engine.control (i).morph || from->values[(size_t) i] < 0.0f || to->values[(size_t) i] < 0.0f) continue;
            const float d = to->values[(size_t) i] - from->values[(size_t) i];
            if (std::abs (d) < 0.004f) continue;
            bool had = false;
            for (auto& l : kn.links) if (l.ctl == i) { l.amount = juce::jlimit (-1.0f, 1.0f, l.amount + d); had = true; }
            if (! had) kn.links.push_back ({ i, d });
        }
        std::sort (kn.links.begin(), kn.links.end(), [] (const MacroKnob::Link& x, const MacroKnob::Link& y) { return x.ctl < y.ctl; });
        kn.clearSnaps();
    }
    std::vector<float> fold ((size_t) kMorphEnd, 0.0f);
    std::array<float, 16> was {};
    for (int k = 0; k < 16; ++k) was[(size_t) k] = engine.target (cx::kSnap + k);
    for (int k = 0; k < 16; ++k)
    {
        const auto& kn = read[(size_t) k];
        const float v = was[(size_t) k];
        if (twoKinds || stepped[(size_t) k] || v <= 0.0f) continue;       // (a stack knob of the two kinds adds nothing: there is nothing of it to put into the scene)
        std::vector<const Snapshot*> ss;
        for (auto id : kn.snaps) if (auto* sn = findSnap (id)) ss.push_back (sn);
        if (ss.size() < 2) continue;
        const float p = v * (float) (ss.size() - 1);
        const int a = juce::jmin ((int) ss.size() - 2, (int) p);
        const float f = p - (float) a;
        for (int i = 0; i < kMorphEnd; ++i)
        {
            if (! engine.control (i).morph) continue;
            const float s0 = ss[0]->values[(size_t) i], sa = ss[(size_t) a]->values[(size_t) i], sb = ss[(size_t) a + 1]->values[(size_t) i];
            if (s0 < 0.0f || sa < 0.0f || sb < 0.0f) continue;
            fold[(size_t) i] += kn.depth * (sa + (sb - sa) * f - s0);
        }
    }
    std::array<MacroKnob, 16> now;
    std::array<bool, 16> full {};
    std::array<int, 16> to; to.fill (-1);
    std::array<float, 16> at {};
    auto aside = [this] (const MacroKnob& kn, bool stack)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", kn.name); o->setProperty ("kind", stack ? "stack" : "learn"); o->setProperty ("depth", kn.depth);
        juce::Array<var> ids, ln, ls;
        for (size_t i = 0; i < kn.snaps.size(); ++i) { ids.add (kn.snaps[i]); ln.add ((int) kn.keysAt (i)); }
        o->setProperty ("snaps", ids); o->setProperty ("lanes", ln);
        for (const auto& l : kn.links) { auto* lo = new juce::DynamicObject(); lo->setProperty ("c", engine.control (l.ctl).id); lo->setProperty ("d", l.amount); ls.add (var (lo)); }
        for (auto& v : kn.linksKept) ls.add (v);
        o->setProperty ("links", ls);
        for (auto& m : kn.more) o->setProperty (m.name, m.value);
        knobsAside.add (var (o));
    };
    auto stackOf = [] (MacroKnob kn) { kn.links.clear(); kn.linksKept.clear(); kn.depth = 1.0f; return kn; };
    auto macroOf = [] (MacroKnob kn, bool split) { kn.clearSnaps(); if (split) { kn.more.remove ("lib"); kn.more.remove ("libmark"); } return kn; };
    auto tied = [] (const MacroKnob& kn) { return ! kn.links.empty() || kn.linksKept.size() > 0; };
    // those that stay
    for (int k = 0; k < 16; ++k)
    {
        const auto& kn = read[(size_t) k];
        if (isStack (k) && ! kn.snaps.empty()) { now[(size_t) k] = stackOf (kn); full[(size_t) k] = true; to[(size_t) k] = k; at[(size_t) k] = was[(size_t) k]; }
        else if (isMacro (k) && kn.snaps.empty() && tied (kn)) { now[(size_t) k] = macroOf (kn, false); full[(size_t) k] = true; to[(size_t) k] = k; at[(size_t) k] = was[(size_t) k]; }
    }
    // those that move, in order
    auto freeOf = [&full] (bool stack) { for (int k = 0; k < 16; ++k) if ((stack ? isStack (k) : isMacro (k)) && ! full[(size_t) k]) return k; return -1; };
    for (int k = 0; k < 16; ++k)
    {
        const auto& kn = read[(size_t) k];
        const bool hasS = ! kn.snaps.empty(), hasL = tied (kn);
        if (hasS && ! isStack (k))
        {
            if (const int f = freeOf (true); f >= 0)
            {
                now[(size_t) f] = stackOf (kn); full[(size_t) f] = true; to[(size_t) k] = f; at[(size_t) f] = was[(size_t) k];
                if (now[(size_t) f].more.contains ("lib")) now[(size_t) f].more.set ("reseat", true);       // (the library is told its new knob at the next LOAD)
            }
            else aside (stackOf (kn), true);
        }
        if (hasL && (! isMacro (k) || hasS))
        {
            // (tied to knobs, on a stack knob's place or on a knob that had snapshots too: a macro knob of its own)
            if (const int f = freeOf (false); f >= 0)
            {
                now[(size_t) f] = macroOf (kn, hasS); full[(size_t) f] = true; at[(size_t) f] = was[(size_t) k];
                if (! hasS) { to[(size_t) k] = f; if (now[(size_t) f].more.contains ("lib")) now[(size_t) f].more.set ("reseat", true); }
            }
            else aside (macroOf (kn, hasS), false);
        }
    }
    for (int k = 0; k < 16; ++k)
    {
        knobs[(size_t) k] = now[(size_t) k];
        knobs[(size_t) k].picked = 0;
        if (! twoKinds) knobs[(size_t) k].sweptAt = at[(size_t) k];       // (of the two kinds: it has its own, as written)
        engine.set (cx::kSnap + k, full[(size_t) k] ? at[(size_t) k] : 0.0f);
        base[(size_t) (cx::kSnap + k)] = engine.target (cx::kSnap + k);
    }
    // MOTION: each unit goes with the knob it was on (a knob that is no longer anywhere: the unit is put out)
    for (int u = 0; u < cx::kMotUnits; ++u)
    {
        const int on = juce::jlimit (0, 15, (int) std::lround (engine.target (cx::mot (u, 2)) * 15.0f));
        if (to[(size_t) on] == on) continue;
        if (to[(size_t) on] >= 0) engine.set (cx::mot (u, 2), (float) to[(size_t) on] / 15.0f);
        else if (! read[(size_t) on].snaps.empty() || tied (read[(size_t) on])) engine.set (cx::mot (u, 0), 0.0f);
        base[(size_t) cx::mot (u, 2)] = engine.target (cx::mot (u, 2)); base[(size_t) cx::mot (u, 0)] = engine.target (cx::mot (u, 0));
    }
    return fold;
}

// The session made what a file's contents say (see stateVar).
void Session::takeState (const var& root)
{
    settleHolds();                  // (a lane held between two instruments by a knob: the file says which one it has)
    // What this build does not know is carried, not dropped: the next save writes it back.
    static const char* const known[] = { "sf000", "wrote", "libraryId", "defs", "base", "baseId", "controls", "activeLane", "files", "space", "insertFiles", "library", "knobs", "recallNotes", "transition", "finish", "cueInSet" /* (a switch of the forty-first build, taken out with it: let go of, not carried) */, "samples", "arrangement", "stacks", "module", "stackCue", "knobsAside", "knobsUnseated", "stacksGone", "modules", "moduleAt", "keptKnobs", "arrangements", "arrangementAt" };
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
    // (the arrangements kept; the one on the page is the file's "arrangement", whatever its own entry here says)
    arrs.clear(); arrAtId = root.getProperty ("arrangementAt", "").toString();
    if (auto* av = root.getProperty ("arrangements", var()).getArray())
        for (auto& v : *av)
        {
            KeptArr a;
            a.id = v.getProperty ("id", "").toString(); a.name = v.getProperty ("name", "").toString(); a.parts = v.getProperty ("arrangement", var());
            if (a.id.isNotEmpty()) arrs.push_back (a);
        }
    stacks.clear();
    if (auto* sv = root.getProperty ("stacks", var()).getArray())
        for (auto& v : *sv)
        {
            Stack st;
            st.id = v.getProperty ("id", "").toString(); st.name = v.getProperty ("name", "").toString(); st.kind = v.getProperty ("kind", "stack").toString();
            st.kept = v;
            st.here = (bool) v.getProperty ("here", false);
            st.mark = v.getProperty ("mark", "").toString();
            const var ln = v.getProperty ("lanes", var());
            bool parts = false;
            if (auto* ids = v.getProperty ("snaps", var()).getArray())
                for (int i = 0; i < ids->size(); ++i)
                    if (const auto id = (juce::int64) (*ids)[i]; findSnap (id) != nullptr)
                    {
                        st.snaps.push_back (id);
                        st.lanes.push_back (ln.isArray() && i < ln.size() ? ((juce::uint32) (int) ln[i] & kAllKeys) : kAllLanes);
                        parts = parts || st.lanes.back() != kAllLanes;
                    }
            if (! parts) st.lanes.clear();
            // (the web library's, with its snapshots all gone: so has the stack. One made here stays, with nothing in it: it is filled by hand)
            if (! st.snaps.empty() || (st.here && st.kind != "song")) stacks.push_back (st);
        }
    // The knobs, as the file has them
    std::array<MacroKnob, 16> read;
    std::array<bool, 16> stepped {};                // (a session from before the two kinds: the knobs that held a stack to step through)
    if (auto* kn = root.getProperty ("knobs", var()).getArray())
        for (int k = 0; k < 16 && k < kn->size(); ++k)
        {
            const juce::var o = (*kn)[k];
            auto& r = read[(size_t) k];
            r.name = o.getProperty ("name", "").toString();
            r.depth = juce::jlimit (0.0f, 2.0f, (float) o.getProperty ("depth", 1.0));
            stepped[(size_t) k] = (bool) o.getProperty ("pick", false);
            r.sweptAt = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("at", 0.0));
            const var ln = o.getProperty ("lanes", var());
            if (auto* ids = o.getProperty ("snaps", var()).getArray())
                for (int i = 0; i < ids->size(); ++i)
                    if (findSnap ((juce::int64) (*ids)[i]) != nullptr)
                        r.add ((juce::int64) (*ids)[i], ln.isArray() && i < ln.size() ? ((juce::uint32) (int) ln[i] & kAllKeys) : MacroKnob::kAll);
            if (auto* ls = o.getProperty ("links", var()).getArray())
                for (auto& l : *ls)
                {
                    const int i = engine.find (l.getProperty ("c", "").toString());
                    const float d = juce::jlimit (-1.0f, 1.0f, (float) (double) l.getProperty ("d", 0.0));
                    if (reaches (engine, i)) { if (std::abs (d) >= 0.004f) r.links.push_back ({ i, d }); }
                    else r.linksKept.add (l);       // (a knob this build does not have, or cannot push: carried)
                }
            if (auto* ko = o.getDynamicObject())
                for (auto& p : ko->getProperties())
                {
                    static const char* const mine[] = { "name", "depth", "snaps", "links", "pick", "lanes", "at" };
                    bool is = false;
                    for (auto* m : mine) is = is || p.name == juce::Identifier (m);
                    if (! is) r.more.set (p.name, p.value);
                }
        }
    if (auto* other = root.getProperty ("controls", var()).getDynamicObject())
        for (auto& p : other->getProperties())
        {
            const int i = engine.find (p.name.toString());
            if (i >= 0 && ! engine.control (i).snapshot) { engine.set (i, (float) p.value); base[(size_t) i] = engine.target (i); }
            if (i < 0) controlsMore.set (p.name, p.value);
        }
    knobsAside.clear();
    if (auto* as = root.getProperty ("knobsAside", var()).getArray()) knobsAside = *as;
    knobsUnseated.clear();
    if (auto* un = root.getProperty ("knobsUnseated", var()).getArray()) knobsUnseated = *un;
    stacksGone.clear();
    if (auto* sg = root.getProperty ("stacksGone", var()).getArray()) stacksGone = *sg;
    keptKnobs.clear();
    if (auto* kv = root.getProperty ("keptKnobs", var()).getArray())
        for (auto& v : *kv)
        {
            KeptKnob kk;
            kk.id = v.getProperty ("id", "").toString(); kk.stack = v.getProperty ("kind", "").toString() == "stack"; kk.knob = v.getProperty ("knob", var());
            if (kk.id.isNotEmpty() && kk.knob.isObject()) keptKnobs.push_back (kk);
        }
    modules.clear(); moduleAt = root.getProperty ("moduleAt", "").toString();
    if (auto* mv = root.getProperty ("modules", var()).getArray())
        for (auto& v : *mv)
        {
            Module m;
            m.id = v.getProperty ("id", "").toString(); m.name = v.getProperty ("name", "").toString();
            m.knobs = v.getProperty ("knobs", var()); m.motion = v.getProperty ("motion", var());
            if (m.id.isNotEmpty()) modules.push_back (m);
        }
    stackCue = (bool) root.getProperty ("stackCue", false);
    std::vector<float> fold;                        // (what the knobs of a session from before the two kinds were adding to the scene)
    if (const int module = (int) root.getProperty ("module", 0); module >= 2)
    {
        auto kinds = read;
        for (int k = 0; k < 16; ++k)
        {
            if (isMacro (k)) kinds[(size_t) k].clearSnaps();
            else { kinds[(size_t) k].links.clear(); kinds[(size_t) k].linksKept.clear(); }
        }
        if (module >= 3) { for (int k = 0; k < 16; ++k) knobs[(size_t) k] = isCorner (k) ? MacroKnob() : kinds[(size_t) k]; }
        else { std::array<bool, 16> none {}; knobsBroughtOver (kinds, none, true); }      // (module 2: 13 and 16 were stack knobs)
    }
    else fold = knobsBroughtOver (read, stepped);
    activeLane = juce::jlimit (0, kLanes - 1, (int) root.getProperty ("activeLane", 7));
    recallNotes = (bool) root.getProperty ("recallNotes", true);
    transFromVar (root.getProperty ("transition", var()));
    // (FINISH: how much of it, in hundredths; the place nearest that. A set from before it had one: off)
    finishAt = juce::jlimit (0, 4, (int) std::lround ((double) root.getProperty ("finish", 0) / 25.0));
    finishApply();
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
    if (! fold.empty())
    {
        // (brought over: what its sweeping knobs were adding is in the scene now, so it sounds as it was left)
        for (int i = 0; i < kMorphEnd && i < (int) fold.size(); ++i)
            if (fold[(size_t) i] != 0.0f && engine.control (i).morph) base[(size_t) i] = juce::jlimit (0.0f, 1.0f, base[(size_t) i] + fold[(size_t) i]);
        applyMorph (0.0);
        dirty = true;
    }
    lawsSig = 0;
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
    modulesReady();
    arrsReady();
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
    // The four macro knobs: each tied to the knobs one of those snapshots moves, by how far it moves them from the clean
    // slate. The first stack knob (5): the five of them in a row, to sweep through or to choose from with CUE lit.
    static const char* const names[] = { "ENERGY", "BUILD", "ACID", "DIST" };
    const auto* clean = findSnap (1000);
    for (int k = 0; k < kMacroKnobs; ++k)
    {
        knobs[(size_t) k].name = names[k];
        const auto* to = findSnap (1001 + k);
        for (int i = 0; clean != nullptr && to != nullptr && i < kMorphEnd; ++i)
            if (engine.control (i).morph && clean->values[(size_t) i] >= 0.0f && to->values[(size_t) i] >= 0.0f)
                if (const float d = to->values[(size_t) i] - clean->values[(size_t) i]; std::abs (d) >= 0.004f) knobs[(size_t) k].links.push_back ({ i, d });
    }
    knobs[(size_t) kMacroKnobs].name = "TOUR";
    for (int k = 0; k < 5; ++k) knobs[(size_t) kMacroKnobs].add (1001 + k);
    baseId = 1000;
}
} // namespace sf0
