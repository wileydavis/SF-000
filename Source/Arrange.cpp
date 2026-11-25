// The arrangement: snapshots in a line that morph one into the next, played from the clock, and
// printed to files. (Session's own: kept in a file of its own for its size.)
#include "Session.h"
#include "Recorder.h"
#include "core/Loudness.h"
#include "core/Bus.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace sf0
{
using juce::String;
using juce::var;

// How long before a part's bar line it is recalled: inside the beat before it, so that everything
// in the recall sets off on that bar line (a recall's knobs set off on the next beat).
static constexpr double kArrLead = 0.5;
// CUT, played: over this many beats (a little over the fiftieth of a second a crossing takes at
// the least, at any tempo this rig has), so that a cut, too, lands on the bar line.
static constexpr double kArrCut = 0.08;

String Session::morphName (int morph)
{
    static const char* const names[] = { "CUT", "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS", "64 BARS", "128 BARS", "256 BARS" };
    return names[juce::jlimit (0, 11, morph)];
}

int Session::arrBarsBefore (int part) const
{
    int bars = 0;
    for (int i = 0; i < part && i < (int) arrangement.size(); ++i) bars += arrangement[(size_t) i].bars;
    return bars;
}

double Session::arrMorphBars (int part) const
{
    if (part < 0 || part >= (int) arrangement.size()) return 0.0;
    const auto& p = arrangement[(size_t) part];
    return std::min ((double) p.bars, glideBarsFor ((float) juce::jlimit (0, 11, p.morph) / 11.0f));
}

bool Session::arrAdd (juce::int64 snap, int before, juce::uint32 lanes)
{
    if ((int) arrangement.size() >= kArrMost || findSnap (snap) == nullptr) return false;
    ArrPart p;
    p.snap = snap; p.lanes = lanes & kAllLanes;
    // (a part comes in as the one before it is: the same length, the same way in; the first: eight bars, cut)
    const int like = before >= 0 && before < (int) arrangement.size() ? before : (int) arrangement.size() - 1;
    if (like >= 0) { p.bars = arrangement[(size_t) like].bars; p.morph = arrangement[(size_t) like].morph; }
    const int went = before >= 0 && before < (int) arrangement.size() ? before : (int) arrangement.size();
    if (before >= 0 && before < (int) arrangement.size()) arrangement.insert (arrangement.begin() + before, p); else arrangement.push_back (p);
    logAction ("add", snap, { { "to", "arr" }, { "arr", arrAtId }, { "at", went }, { "parts", (int) p.lanes } });
    if (arrOn && before >= 0 && before <= arrAt) ++arrAt;
    if (arrEdit >= 0 && before >= 0 && before <= arrEdit) ++arrEdit;       // (the part being edited is the one it was)
    touch();
    if (onChange) onChange();
    return true;
}

void Session::arrRemove (int part)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    logAction ("drop", arrangement[(size_t) part].snap, { { "from", "arr" }, { "arr", arrAtId }, { "at", part } });
    arrangement.erase (arrangement.begin() + part);
    if (arrOn) { if (part <= arrAt) --arrAt; if (arrangement.empty()) arrStop(); }
    if (arrEdit == part) arrEdit = -1; else if (arrEdit > part) --arrEdit;  // (the part being edited taken out: there is nothing to keep it into)
    touch();
    if (onChange) onChange();
}

void Session::arrMove (int part, int by)
{
    const int to = part + by;
    if (part < 0 || part >= (int) arrangement.size() || to < 0 || to >= (int) arrangement.size() || by == 0) return;
    logAction ("move", arrangement[(size_t) part].snap, { { "in", "arr" }, { "arr", arrAtId }, { "at", part }, { "to", to } });
    std::swap (arrangement[(size_t) part], arrangement[(size_t) to]);
    arrEdit = -1;                       // (what came before the part being edited is no longer what it was brought with)
    touch();
    if (onChange) onChange();
}

void Session::arrSet (int part, int bars, int morph)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    auto& p = arrangement[(size_t) part];
    if (p.isBreak()) { if (bars >= 0) arrSetBreak (part, bars * 4); return; }        // (a BREAK's length is its own: in quarters)
    if (bars >= 0) p.bars = juce::jlimit (1, kArrMaxBars, bars);
    if (morph >= 0) p.morph = juce::jlimit (0, 11, morph);
    touch();
}

// (the hundred and eighteenth build) Wiley, Nov 24: "and a way to adjust the levels for the transition samples (in and hit).
// adustable for each snapshot in the arrnagement". Each part's lead-in and hit, louder or softer than the TRANSITION's LEVEL.
void Session::arrTransDb (int part, int slot, int db)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    auto& p = arrangement[(size_t) part];
    int& v = slot == 0 ? p.inDb : p.hitDb;
    const int to = juce::jlimit (-24, 12, db);
    if (to == v) return;
    v = to;
    touch();
    if (onChange) onChange();
}

// ---- BREAK (the hundred and seventeenth build). Wiley, Nov 24: "for arrangement, can you give me a "snapshot" block i can add
// between snapshots that can be as short as 1/4 bar but are silent but with whatever reverb or delay or effect tails from what
// came before it passed through. for dramatic beat pauses." Asked where a short one sits: "Ends on next bar" (it ends where
// the next part begins; whole bars are its own, a quarter or a half or three taken from the end of the part before).
juce::String Session::breakName (int q)
{
    q = juce::jmax (1, q);
    const int whole = q / 4, part = q % 4;
    static const char* const frac[] = { "", "1/4", "1/2", "3/4" };
    juce::String s;
    if (whole > 0) s << whole;
    if (part > 0) s << (whole > 0 ? " " : "") << frac[part];
    return s + (q <= 4 ? " BAR" : " BARS");
}

int Session::arrAddBreak (int after, int quarters)
{
    if ((int) arrangement.size() >= kArrMost || print != nullptr) return -1;
    ArrPart p;
    p.breakQ = juce::jlimit (1, 64, quarters);
    p.bars = p.breakQ / 4;
    const int at = after >= 0 && after < (int) arrangement.size() ? after + 1 : (int) arrangement.size();
    arrangement.insert (arrangement.begin() + at, p);
    logAction ("add_break", 0, { { "arr", arrAtId }, { "at", at }, { "quarters", p.breakQ } });
    if (arrOn && at <= arrAt) ++arrAt;
    if (arrEdit >= 0 && at <= arrEdit) ++arrEdit;
    arrBreakFor = -1;
    touch();
    if (onChange) onChange();
    return at;
}

void Session::arrSetBreak (int part, int quarters)
{
    if (part < 0 || part >= (int) arrangement.size() || ! arrangement[(size_t) part].isBreak()) return;
    auto& p = arrangement[(size_t) part];
    p.breakQ = juce::jlimit (1, 64, quarters);
    p.bars = p.breakQ / 4;
    arrBreakFor = -1;                       // (given to the engine again, as it now is, while it is still ahead)
    touch();
    if (onChange) onChange();
}

void Session::arrBreakAhead()
{
    if (! arrOn || arrWait) return;
    const int n = (int) arrangement.size();
    for (int j : { arrAt, arrAt + 1 })
    {
        if (j < 0 || j >= n || ! arrangement[(size_t) j].isBreak() || (j > 0 && arrangement[(size_t) (j - 1)].isBreak())) continue;
        if (arrBreakFor == j) return;
        // the BREAK, with any straight after it, as one: it ends where the part after them begins
        int last = j, q = 0;
        while (last < n && arrangement[(size_t) last].isBreak()) q += arrangement[(size_t) last++].breakQ;
        const double end = arrZero + (double) arrBarsBefore (last) * 4.0;
        engine.setBreak (end - (double) q, end);
        arrBreakFor = j;
        return;
    }
}

void Session::arrLanes (int part, juce::uint32 lanes)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    arrangement[(size_t) part].lanes = lanes & kAllLanes;
    touch();
    if (onChange) onChange();
}

void Session::recallOver (juce::int64 id, double beats, bool notes, juce::uint32 lanes)
{
    glideOver = std::max (0.0, beats);
    Scope sc = scopeOf (lanes & kAllLanes);         // (all of them: everything, as a recall of the whole snapshot)
    sc.notes = notes;
    recall (id, sc);
    glideOver = -1.0;
}

void Session::arrNotes (int part, bool on)
{
    if (part < 0 || part >= (int) arrangement.size() || arrangement[(size_t) part].notes == on) return;
    arrangement[(size_t) part].notes = on;
    touch();
    if (onChange) onChange();
}

void Session::arrTransSet (int part, int in, int hit)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    auto& p = arrangement[(size_t) part];
    // (its own file only if it has one)
    p.transIn = juce::jlimit (0, p.inFile.isNotEmpty() ? 2 : 1, in); p.transHit = juce::jlimit (0, p.hitFile.isNotEmpty() ? 2 : 1, hit);
    touch();
    if (onChange) onChange();
}

void Session::arrTransStep (int part, int slot)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    auto& p = arrangement[(size_t) part];
    int& mode = slot == 0 ? p.transIn : p.transHit;
    const bool own = (slot == 0 ? p.inFile : p.hitFile).isNotEmpty();
    mode = mode == 0 ? (own ? 2 : 1) : mode == 2 ? 1 : 0;              // not; its own file (if it has one); the bar's; not
    touch();
    if (onChange) onChange();
}

bool Session::arrTransFile (int part, int slot, const juce::File& f)
{
    if (part < 0 || part >= (int) arrangement.size()) return false;
    // (the library's own copy of it, read once here so that nothing is read when it is set off)
    const juce::String kept = keepSample (f.getFullPathName());
    if (engine.transAsset (kept) == nullptr) return false;
    auto& p = arrangement[(size_t) part];
    (slot == 0 ? p.inFile : p.hitFile) = kept;
    (slot == 0 ? p.transIn : p.transHit) = 2;
    touch();
    if (onChange) onChange();
    return true;
}

void Session::arrRecall (int part, double beats)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    // (printed, a part brings its notes whatever NOTES says: there is nothing outside to play them)
    // (its notes: as the part's own N key says, lit as a part starts. Not as the library's NOTES says: that one is for a recall
    // made by hand, and a switch on another page silently emptied every part of its notes.)
    notesOnLine = true;
    recallOver (arrangement[(size_t) part].snap, beats, arrangement[(size_t) part].notes, arrangement[(size_t) part].lanes);
    notesOnLine = false;
}

// ---- what came before
static bool samePattern (const Pattern& a, const Pattern& b)
{
    if (a.length != b.length) return false;
    for (int i = 0; i < a.length && i < kMaxSteps; ++i)
    {
        const auto& x = a.steps[(size_t) i]; const auto& y = b.steps[(size_t) i];
        if (x.on != y.on || x.lock != y.lock) return false;
        if (x.on && (x.vel != y.vel || x.gate != y.gate || x.ratchet != y.ratchet || x.note != y.note || x.octave != y.octave)) return false;
    }
    // (the hundred and eleventh build) what the steps learnt too: a lock changed and nothing else is a change of what plays
    auto within = [] (const Pattern& p, const Pattern& q)
    {
        for (auto& k : p.locks)
            if (k.step < p.length && k.step < kMaxSteps && std::abs (lockOff (q, k.step, k.ctl) - k.off) > 1.0e-6f) return false;
        return true;
    };
    return within (a, b) && within (b, a);
}

Snapshot Session::arrStateAt (int part, std::array<bool, kLanes>& notes) const
{
    // The rig as it stands, whole, and each part's share of its snapshot laid over it in turn: what a recall of that
    // part would change (Session::recall), kept as a state to lay the next on instead of being made at once.
    Snapshot acc = capture();
    acc.hasPlayed = true;
    for (int l = 0; l < kLanes; ++l) { acc.heardOn[(size_t) l] = false; acc.seqMuted[(size_t) l] = engine.target (cx::lane (l, cx::kSeqMute)) > 0.5f; }
    notes.fill (false);
    for (int i = 0; i <= part && i < (int) arrangement.size(); ++i)
    {
        const auto& p = arrangement[(size_t) i];
        const Snapshot* s = findSnap (p.snap);
        if (s == nullptr) continue;
        const Scope sc = scopeOf (p.lanes & kAllLanes);
        // (a control the snapshot says nothing of stays as it is)
        auto take = [&] (int from, int to) { for (int c = from; c < to && c < (int) s->values.size() && c < (int) acc.values.size(); ++c) if (s->values[(size_t) c] >= 0.0f) acc.values[(size_t) c] = s->values[(size_t) c]; };
        for (int l = 0; l < kLanes; ++l)
        {
            if (! sc.lane[(size_t) l]) continue;
            // the lane's sound: all it stores but its generator's four knobs, which go with its notes
            take (l * cx::kPerLane, cx::gen (l, 0)); take (cx::gen (l, 0) + 4, (l + 1) * cx::kPerLane);
            acc.inserts[(size_t) l] = s->inserts[(size_t) l];
            if (s->hasSetup) { acc.setup[(size_t) l] = s->setup[(size_t) l]; acc.insSetup[(size_t) l] = s->insSetup[(size_t) l]; }
            if (s->hasInsFiles) acc.insFiles[(size_t) l] = s->insFiles[(size_t) l];
            if (s->hasFiles && s->laneFiles[(size_t) l].isNotEmpty()) acc.laneFiles[(size_t) l] = s->laneFiles[(size_t) l];
            if (! p.notes) continue;
            take (cx::gen (l, 0), cx::gen (l, 0) + 4);
            acc.patterns[(size_t) l] = s->patterns[(size_t) l]; acc.gen[(size_t) l] = s->gen[(size_t) l];
            acc.heard[(size_t) l] = s->heard[(size_t) l]; acc.heardNotes[(size_t) l] = s->heardNotes[(size_t) l];
            acc.heardOn[(size_t) l] = s->hasPlayed && s->heardOn[(size_t) l];
            if (s->hasPlayed) acc.seqMuted[(size_t) l] = s->seqMuted[(size_t) l];
            notes[(size_t) l] = true;
        }
        if (sc.returns) { take (cx::kRet, cx::kBus); if (s->hasSetup) acc.retSetup = s->retSetup; }
        if (sc.bus) { take (cx::kBus, cx::kRetMS); if (s->hasBus) acc.busSetup = s->busSetup; }
        if (sc.master) take (cx::kMaster, cx::kSnap);
    }
    return acc;
}

void Session::arrBring (int part, double beats)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    std::array<bool, kLanes> notes;
    const Snapshot st = arrStateAt (part, notes);
    stacksLeft (-1);
    glideOver = std::max (0.0, beats);
    notesOnLine = true;
    applySnapshot (st, recallGlide(), &notes);
    notesOnLine = false;
    glideOver = -1.0;
    baseId = arrangement[(size_t) part].snap;
    selStep = -1;
    touch();
    if (onChange) onChange();
}

// ---- EDIT
bool Session::usedBesides (juce::int64 snap, int part) const
{
    for (int i = 0; i < (int) arrangement.size(); ++i) if (i != part && arrangement[(size_t) i].snap == snap) return true;
    for (auto& k : knobs) for (auto id : k.snaps) if (id == snap) return true;
    for (auto& st : stacks) for (auto id : st.snaps) if (id == snap) return true;
    // (a knob or a module kept beside the chassis holds its snapshots as the file writes them: by their numbers)
    const juce::String number (snap);
    for (auto& k : keptKnobs) if (juce::JSON::toString (k.knob, true).contains (number)) return true;
    for (auto& m : modules) if (m.id != moduleAt && juce::JSON::toString (m.knobs, true).contains (number)) return true;
    for (auto& a : arrs) if (a.id != arrAtId && juce::JSON::toString (a.parts, true).contains (number)) return true;        // (an arrangement kept, not the one on the page)
    return false;
}

bool Session::arrEditBegin (int part)
{
    if (part < 0 || part >= (int) arrangement.size() || print != nullptr || arrangement[(size_t) part].isBreak()) return false;
    if (arrOn) arrStop();
    if (cueId != 0) cue (0);
    // (stopped, so that nothing of the way there is heard; then it plays, from the top of its bars)
    engine.setRunning (false);
    arrBring (part, 0.0);
    arrEdit = part;
    arrEditCopy = false;
    arrEditWas = capture();
    engine.setRunning (true);
    if (onChange) onChange();
    return true;
}

void Session::arrEditDrop()
{
    if (arrEdit < 0) return;
    const int part = arrEdit;
    arrEdit = -1;
    // (a copy dropped before it was ever kept is the part beside it again: it goes)
    if (arrEditCopy) { arrEditCopy = false; arrRemove (part); return; }
    if (onChange) onChange();
}

int Session::arrCopy (int part, bool after)
{
    if (part < 0 || part >= (int) arrangement.size() || (int) arrangement.size() >= kArrMost || print != nullptr || arrangement[(size_t) part].isBreak()) return -1;
    ArrPart p = arrangement[(size_t) part];
    p.transIn = 0; p.transHit = 0; p.inFile = {}; p.hitFile = {}; p.inDb = p.hitDb = 0;      // (its lead-in and hit belong where they were put)
    p.edited = false;                                                   // (its snapshot is the part's: an EDIT of the copy makes its own)
    const int at = after ? part + 1 : part;
    arrangement.insert (arrangement.begin() + at, p);
    logAction ("add", p.snap, { { "to", "arr" }, { "arr", arrAtId }, { "at", at }, { "parts", (int) p.lanes }, { "copy", part } });
    if (arrOn && at <= arrAt) ++arrAt;
    if (arrEdit >= 0 && at <= arrEdit) ++arrEdit;
    touch();
    if (! arrEditBegin (at)) { if (onChange) onChange(); return at; }
    arrEditCopy = true;
    return at;
}

juce::int64 Session::arrEditKeep()
{
    if (arrEdit < 0 || arrEdit >= (int) arrangement.size()) { arrEdit = -1; return 0; }
    const int part = arrEdit;
    const juce::int64 was = arrangement[(size_t) part].snap;
    // What was changed since it was brought, by the part of the rig a key stands for (what sounds: a snapshot holds each
    // knob as it sounded). A lane's notes are its own matter, and N's.
    const Snapshot now = capture();
    const Snapshot& old = arrEditWas;
    auto differs = [&] (int from, int to)
    {
        for (int c = from; c < to && c < (int) now.values.size() && c < (int) old.values.size(); ++c)
            if (std::abs (now.values[(size_t) c] - old.values[(size_t) c]) > 1.0e-4f) return true;
        return false;
    };
    juce::uint32 changed = 0;
    bool notesChanged = false;
    for (int l = 0; l < kLanes; ++l)
    {
        const bool sound = differs (l * cx::kPerLane, cx::gen (l, 0)) || differs (cx::gen (l, 0) + 4, (l + 1) * cx::kPerLane)
                           || now.inserts[(size_t) l] != old.inserts[(size_t) l] || now.setup[(size_t) l] != old.setup[(size_t) l] || now.insSetup[(size_t) l] != old.insSetup[(size_t) l]
                           || ! (now.insFiles[(size_t) l] == old.insFiles[(size_t) l]) || now.laneFiles[(size_t) l] != old.laneFiles[(size_t) l];
        const bool played = differs (cx::gen (l, 0), cx::gen (l, 0) + 4) || now.gen[(size_t) l] != old.gen[(size_t) l] || ! samePattern (now.patterns[(size_t) l], old.patterns[(size_t) l]);
        if (sound || played) changed |= 1u << l;
        notesChanged = notesChanged || played;
    }
    if (differs (cx::kRet, cx::kBus) || now.retSetup != old.retSetup) changed |= 1u << kLanes;
    if (differs (cx::kBus, cx::kRetMS) || now.busSetup != old.busSetup) changed |= 1u << (kLanes + 1);
    if (differs (cx::kMaster, cx::kSnap)) changed |= 1u << (kLanes + 2);

    // (the snapshot an EDIT made for this part before, if nothing else holds it, makes way: the library does not fill with every try)
    const bool makesWay = arrangement[(size_t) part].edited && ! usedBesides (was, part);
    juce::String name;
    if (const auto* s = findSnap (was))
    {
        name = s->name;
        if (! makesWay)
        {
            // named after it: ACID OPEN, then ACID OPEN 2, 3 ...
            // (one that is itself a numbered version of a snapshot here is numbered on from that one: ACID OPEN 2 gives
            // ACID OPEN 3. A name that only ends in a number, SNAP 008, is a name: SNAP 008 2)
            auto taken = [this] (const juce::String& nm) { for (auto& x : library) if (x.name == nm) return true; return false; };
            juce::String stem = name;
            const int sp = stem.lastIndexOfChar (' ');
            if (sp > 0 && stem.substring (sp + 1).isNotEmpty() && stem.substring (sp + 1).containsOnly ("0123456789") && taken (stem.substring (0, sp))) stem = stem.substring (0, sp);
            for (int n = 2; n < 1000; ++n) { name = stem.substring (0, 24 - 1 - juce::String (n).length()).trimEnd() + " " + juce::String (n); if (! taken (name)) break; }
        }
    }
    const juce::int64 id = take();
    if (id == 0 || findSnap (id) == nullptr) return 0;
    {
        auto& p = arrangement[(size_t) part];
        arrEditLit = changed & ~p.lanes & kAllLanes;
        p.snap = id;
        p.lanes = (p.lanes | changed) & kAllLanes;
        if (notesChanged) p.notes = true;
        p.edited = true;
    }
    if (makesWay) remove (was);
    if (name.isNotEmpty()) rename (id, name);
    arrEdit = -1;
    arrEditCopy = false;
    baseId = id;
    touch();
    if (onChange) onChange();
    return id;
}

// ---- TO STACK
int Session::arrOpenParts() const
{
    int n = 0;
    for (auto& p : arrangement) if ((p.lanes & kAllLanes) != kAllLanes) ++n;
    return n;
}

int Session::arrToStack()
{
    if (arrangement.empty()) return -1;
    const int at = rackNew ("TRACK");
    // (each with its part's keys, N among them: put on a knob, a part that brought its notes brings them there)
    {
        const Quiet q (*this);          // (one thing done: the arrangement kept as a stack, logged once)
        for (auto& p : arrangement) if (! p.isBreak()) rackAdd (at, p.snap, (p.lanes & kAllLanes) | (p.notes ? kNotesKey : 0u));      // (a BREAK has no chip)
    }
    logAction ("arr_to_stack", 0, { { "arr", arrAtId }, { "stack", stacks[(size_t) at].id }, { "name", stacks[(size_t) at].name }, { "chips", (int) arrangement.size() } });
    return at;
}

void Session::arrPlay (int from)
{
    if (arrangement.empty()) return;
    from = juce::jlimit (0, (int) arrangement.size() - 1, from);
    arrEdit = -1;                       // (an EDIT under way is dropped: the rig is the arrangement's again)
    // (a chip that was being listened to is put down: its sound and the arrangement's were heard together. One pressed while
    // the arrangement plays is still heard, as one pressed while the set plays is.)
    if (cueId != 0) cue (0);
    logAction ("play", 0, { { "arr", arrAtId }, { "from", from }, { "parts", (int) arrangement.size() } });
    arrOn = true; arrTicks = 0; arrTransFor = -1; arrBreakFor = -1; engine.clearBreak();
    for (auto& p : arrangement) { if (p.transIn == 2) engine.transAsset (p.inFile); if (p.transHit == 2) engine.transAsset (p.hitFile); }       // (the parts' own files, read before it plays)
    if (! engine.isRunning())
    {
        // from stopped: that part is there at once, with what every part before it left (FROM HERE), and the clock sets off from the top
        if (from > 0) arrBring (from, 0.0); else arrRecall (from, 0.0);
        arrAt = from;
        logAction ("part", arrangement[(size_t) from].snap, { { "arr", arrAtId }, { "at", from }, { "bars", arrangement[(size_t) from].bars }, { "morph", arrangement[(size_t) from].morph }, { "parts", (int) (arrangement[(size_t) from].lanes & kAllLanes) } });
        arrZero = -(double) arrBarsBefore (from) * 4.0;
        arrStarts = engine.starts(); arrWait = true;       // (until the clock has set off, the place it says is the last run's)
        {
            // (a BREAK at or just after the part it starts from: given before the clock sets off, so its first beat is in it)
            const juce::ScopedValueSetter<bool> notWaiting (arrWait, false);
            arrBreakAhead();
        }
        engine.setRunning (true);
    }
    else
    {
        // while it runs: that part comes in on the next bar line it can be ready for
        const double bar = std::ceil ((engine.songPos() + kArrLead + 0.25) / 4.0) * 4.0;
        arrZero = bar - (double) arrBarsBefore (from) * 4.0;
        arrAt = from - 1; arrWait = false;
        arrBringNext = from > 0;
    }
    if (useDisk) startTimer (15);
    if (onChange) onChange();
}

void Session::arrStop()
{
    if (! arrOn) return;
    arrOn = false; arrAt = -1;
    engine.clearBreak(); arrBreakFor = -1;
    engine.setRunning (false);
    if (onChange) onChange();
}

double Session::arrPos() const
{
    if (! arrOn || arrWait) return arrOn ? (double) arrBarsBefore (juce::jmax (0, arrAt)) * 4.0 : 0.0;
    return engine.songPos() - arrZero;
}

void Session::arrTick()
{
    if (! arrOn) return;
    if (arrWait) { if (engine.starts() == arrStarts) return; arrWait = false; }
    if (! engine.isRunning()) { arrOn = false; arrAt = -1; engine.clearBreak(); arrBreakFor = -1; if (onChange) onChange(); return; }      // stopped by hand: it is put down
    const double pos = engine.songPos() - arrZero;
    const int next = arrAt + 1;
    // A part that is come into with the TRANSITION: set off its BARS before the part's bar line (at once, if the part before
    // it is shorter than that), so that the lead-in ends on that line and the hit lands with the part.
    if (next < (int) arrangement.size() && (arrangement[(size_t) next].transIn != 0 || arrangement[(size_t) next].transHit != 0) && arrTransFor != next
        && pos >= (double) arrBarsBefore (next) * 4.0 - (double) transBars * 4.0)
    {
        arrTransFor = next;
        transApply();
        // what this one plays: the bar's lead-in and hit, or the part's own files (one that cannot be read: the bar's)
        const auto& np = arrangement[(size_t) next];
        auto pick = [this] (int mode, const juce::String& file, int& kind, const sf::SampleData*& smp)
        {
            kind = -1; smp = nullptr;
            if (mode == 0) kind = Transition::kOff;
            else if (mode == 2) if (const auto* a = engine.transAsset (file)) { kind = Transition::kFile; smp = a; }
        };
        int lk = -1, hk = -1; const sf::SampleData* ls = nullptr; const sf::SampleData* hs = nullptr;
        pick (np.transIn, np.inFile, lk, ls); pick (np.transHit, np.hitFile, hk, hs);
        const double at = engine.transFireTo (arrZero + (double) arrBarsBefore (next) * 4.0, lk, ls, hk, hs,
                                              juce::Decibels::decibelsToGain ((float) np.inDb), juce::Decibels::decibelsToGain ((float) np.hitDb));
        if (at >= 0.0) transAt = at;
    }
    arrBreakAhead();
    if (next < (int) arrangement.size())
    {
        if (pos >= (double) arrBarsBefore (next) * 4.0 - kArrLead)
        {
            // (FROM HERE, come into while the clock runs: with what every part before it left, over its own way in. A BREAK
            // recalls nothing: the engine was given its place, and what it leaves is what the part after it comes into.)
            if (arrangement[(size_t) next].isBreak()) {}
            else if (arrBringNext) arrBring (next, std::max (kArrCut, arrMorphBars (next) * 4.0)); else arrRecall (next, std::max (kArrCut, arrMorphBars (next) * 4.0));
            if (! arrangement[(size_t) next].isBreak()) arrBringNext = false;
            arrAt = next;
            // (the log: the arrangement has come to this part. What follows what, as it was played)
            logAction ("part", arrangement[(size_t) next].snap, { { "arr", arrAtId }, { "at", next }, { "bars", arrangement[(size_t) next].bars }, { "morph", arrangement[(size_t) next].morph }, { "parts", (int) (arrangement[(size_t) next].lanes & kAllLanes) } });
            if (onChange) onChange();
        }
    }
    else if (! printRig && pos >= (double) arrBars() * 4.0 - 0.06)
    {
        // its end: the clock stops, a moment short of the bar line so that nothing of the bar after is begun
        // (a print stops itself, on the sample)
        arrOn = false; arrAt = -1;
        engine.setRunning (false);
        engine.clearBreak(); arrBreakFor = -1;
        if (onChange) onChange();
    }
}

// ---------------------------------------------------------------------- the arrangements kept
static juce::String arrsNamed (juce::String want, const std::vector<Session::KeptArr>& all, int but = -1)
{
    want = want.trim().toUpperCase().substring (0, 24);
    if (want.isEmpty()) want = "ARRANGEMENT " + juce::String ((int) all.size() + 1);
    auto taken = [&] (const juce::String& n) { for (int i = 0; i < (int) all.size(); ++i) if (i != but && all[(size_t) i].name == n) return true; return false; };
    juce::String name = want;
    for (int n = 2; taken (name); ++n) name = want.substring (0, 21) + " " + juce::String (n);
    return name;
}

static juce::String arrsId()
{
    juce::String id ("arr");
    juce::Random r;
    for (int i = 0; i < 13; ++i) id << juce::String::charToString ((juce::juce_wchar) "abcdefghijklmnopqrstuvwxyz0123456789"[r.nextInt (36)]);
    return id;
}

int Session::arrsIndex() const
{
    for (int i = 0; i < (int) arrs.size(); ++i) if (arrs[(size_t) i].id == arrAtId) return i;
    return -1;
}

juce::String Session::arrsName() const { const int i = arrsIndex(); return i >= 0 ? arrs[(size_t) i].name : juce::String(); }

void Session::arrsReady()
{
    if (arrsIndex() >= 0) return;
    // (a session from before them, or a new one: the arrangement it has is its first)
    KeptArr a;
    a.id = arrsId(); a.name = arrsNamed ("ARRANGEMENT 1", arrs);
    a.parts = arrToVar();
    arrs.push_back (a);
    arrAtId = a.id;
}

int Session::arrsCopy (const juce::String& name)
{
    arrsReady();
    const int was = arrsIndex();
    arrs[(size_t) was].parts = arrToVar();              // (the one it is a copy of, kept as it stands)
    KeptArr a;
    a.id = arrsId();
    a.name = arrsNamed (name.isNotEmpty() ? name : arrs[(size_t) was].name, arrs);
    a.parts = arrToVar();                               // (its own: not the same object as the first's)
    arrs.push_back (a);
    arrAtId = a.id;
    touch();
    if (onChange) onChange();
    return (int) arrs.size() - 1;
}

int Session::arrsNew (const juce::String& name)
{
    arrsReady();
    KeptArr a;
    a.id = arrsId(); a.name = arrsNamed (name, arrs);
    arrs.push_back (a);                                 // (its parts: none)
    if (! arrsLoad ((int) arrs.size() - 1)) { arrs.pop_back(); return -1; }
    return (int) arrs.size() - 1;
}

bool Session::arrsLoad (int index)
{
    arrsReady();
    const int was = arrsIndex();
    if (index < 0 || index >= (int) arrs.size() || index == was || print != nullptr) return false;
    arrs[(size_t) was].parts = arrToVar();              // (the one on the page, kept as it stands)
    if (arrOn) arrStop();                               // (what plays is the page's: it stops, as HOLD: CLEAR's would)
    if (arrEdit >= 0) arrEditDrop();
    const KeptArr a = arrs[(size_t) index];
    arrFromVar (a.parts);
    arrAtId = a.id;
    touch();
    if (onChange) onChange();
    return true;
}

void Session::arrsRename (int index, const juce::String& name)
{
    if (index < 0 || index >= (int) arrs.size() || name.trim().isEmpty()) return;
    arrs[(size_t) index].name = arrsNamed (name, arrs, index);
    touch();
    if (onChange) onChange();
}

bool Session::arrsRemove (int index)
{
    arrsReady();
    if (index < 0 || index >= (int) arrs.size() || arrs.size() < 2) return false;
    // (the one on the page: its neighbour takes the page first, so the page is never nobody's)
    if (index == arrsIndex() && ! arrsLoad (index > 0 ? index - 1 : 1)) return false;
    arrs.erase (arrs.begin() + index);
    sweepSamples();                                     // (the lead-ins and hits only its parts held go with it)
    touch();
    if (onChange) onChange();
    return true;
}

juce::String Session::arrsSays (int index) const
{
    if (index < 0 || index >= (int) arrs.size()) return {};
    const var v = index == arrsIndex() ? arrToVar() : arrs[(size_t) index].parts;
    int n = 0, bars = 0;
    juce::StringArray names;
    if (auto* parts = v.getProperty ("parts", var()).getArray())
        for (auto& pv : *parts)
            if (const auto* sn = findSnap ((juce::int64) pv.getProperty ("snap", 0)))
            {
                ++n; bars += juce::jlimit (1, kArrMaxBars, (int) pv.getProperty ("bars", 8));
                if (names.size() < 12) names.add (sn->name);
            }
    if (n == 0) return "NO PARTS YET";
    const int secs = juce::roundToInt ((double) bars * 4.0 * 60.0 / engine.tempo());
    juce::String t;
    t << n << (n == 1 ? " PART, " : " PARTS, ") << bars << (bars == 1 ? " BAR, " : " BARS, ") << secs / 60 << ":" << juce::String (secs % 60).paddedLeft ('0', 2) << ": " << names.joinIntoString (" > ");
    return t;
}

// --------------------------------------------------------------------------------- the file
std::vector<Session::PartSeen> Session::arrsParts (int index) const
{
    std::vector<PartSeen> out;
    if (index < 0 || index >= (int) arrs.size()) return out;
    if (index == arrsIndex()) { for (const auto& p : arrangement) out.push_back ({ p.snap, p.bars }); return out; }
    if (auto* parts = arrs[(size_t) index].parts.getProperty ("parts", var()).getArray())
        for (const auto& p : *parts) out.push_back ({ (juce::int64) p.getProperty ("snap", 0), (int) p.getProperty ("bars", 8) });
    return out;
}

var Session::arrToVar() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<var> parts;
    for (const auto& p : arrangement)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("snap", p.snap);
        po->setProperty ("bars", p.bars);
        if (p.isBreak()) po->setProperty ("break", p.breakQ);                                                       // (absent: a part, not a BREAK)
        po->setProperty ("morph", glideBarsFor ((float) juce::jlimit (0, 11, p.morph) / 11.0f));       // (in bars: a later build may have other lengths)
        if ((p.lanes & kAllLanes) != kAllLanes) po->setProperty ("lanes", (juce::int64) (p.lanes & kAllLanes));     // (absent: all of them)
        if (p.transIn != 0) po->setProperty ("transIn", p.transIn);                                                 // (absent: not)
        if (p.transHit != 0) po->setProperty ("transHit", p.transHit);
        if (p.inDb != 0) po->setProperty ("inDb", p.inDb);                                                         // (absent: nought)
        if (p.hitDb != 0) po->setProperty ("hitDb", p.hitDb);
        if (p.inFile.isNotEmpty()) po->setProperty ("inFile", refOf (p.inFile));                                   // (a copy in the library: from its folder down)
        if (p.hitFile.isNotEmpty()) po->setProperty ("hitFile", refOf (p.hitFile));
        if (p.edited) po->setProperty ("edited", true);                                                             // (absent: no)
        if (! p.notes) po->setProperty ("notes", false);                                                           // (absent: it brings them)                                                              // (absent: no)
        for (auto& m : p.more) po->setProperty (m.name, m.value);
        parts.add (var (po));
    }
    o->setProperty ("parts", parts);
    for (auto& m : arrMore) o->setProperty (m.name, m.value);
    return var (o);
}

void Session::arrFromVar (const var& v)
{
    arrangement.clear(); arrMore.clear();
    arrEdit = -1;
    auto* o = v.getDynamicObject();
    if (o == nullptr) return;
    for (auto& p : o->getProperties()) if (p.name != juce::Identifier ("parts")) arrMore.set (p.name, p.value);
    if (auto* parts = v.getProperty ("parts", var()).getArray())
        for (auto& pv : *parts)
        {
            ArrPart p;
            p.snap = (juce::int64) pv.getProperty ("snap", 0);
            if (const int q = (int) pv.getProperty ("break", 0); q > 0)
            {
                p.snap = 0; p.breakQ = juce::jlimit (1, 64, q); p.bars = p.breakQ / 4;
                arrangement.push_back (p);
                continue;
            }
            if (findSnap (p.snap) == nullptr) continue;             // (its snapshot has gone: so has the part)
            p.bars = juce::jlimit (1, kArrMaxBars, (int) pv.getProperty ("bars", 8));
            const double mb = (double) pv.getProperty ("morph", 0.0);
            p.morph = 0;
            for (int i = 0; i < 12; ++i) if (glideBarsFor ((float) i / 11.0f) <= mb + 1.0e-9) p.morph = i;      // the nearest length this build has, not over it
            if (auto* po = pv.getDynamicObject())
                for (auto& q : po->getProperties())
                    if (q.name != juce::Identifier ("snap") && q.name != juce::Identifier ("break") && q.name != juce::Identifier ("bars") && q.name != juce::Identifier ("morph") && q.name != juce::Identifier ("lanes") && q.name != juce::Identifier ("notes") && q.name != juce::Identifier ("transIn") && q.name != juce::Identifier ("transHit") && q.name != juce::Identifier ("inFile") && q.name != juce::Identifier ("hitFile") && q.name != juce::Identifier ("inDb") && q.name != juce::Identifier ("hitDb") && q.name != juce::Identifier ("edited")) p.more.set (q.name, q.value);
            p.transIn = juce::jlimit (0, 2, (int) pv.getProperty ("transIn", 0)); p.transHit = juce::jlimit (0, 2, (int) pv.getProperty ("transHit", 0));
            p.inDb = juce::jlimit (-24, 12, (int) pv.getProperty ("inDb", 0)); p.hitDb = juce::jlimit (-24, 12, (int) pv.getProperty ("hitDb", 0));
            p.inFile = pathOf (pv.getProperty ("inFile", "").toString()); p.hitFile = pathOf (pv.getProperty ("hitFile", "").toString());
            p.notes = (bool) pv.getProperty ("notes", true);
            p.edited = (bool) pv.getProperty ("edited", false);
            if (pv.hasProperty ("lanes")) p.lanes = (juce::uint32) (juce::int64) pv.getProperty ("lanes", (juce::int64) kAllLanes) & kAllLanes;
            arrangement.push_back (p);
        }
}

// -------------------------------------------------------------------------------- the print
// A rig of its own is made for it, from the session exactly as it stands (what the file would hold:
// the library, the scene, the snapshot knobs and their MOTION, the arrangement), and the
// arrangement is played on that, its render called here instead of by an audio device and the
// sink a take would be given written straight to files. Every module in it starts from its
// beginning, so the same arrangement prints the same file; and the rig that is being played is
// not touched (it only waits: one rig sounds at a time).
// The first samples each file is given are left out: the time the rig holds its sound back by, so
// that bar 1 begins on the file's first sample.
//
// What it leaves is for a mastering engineer as much as for the player (DECISIONS 566):
//   00 SET.wav                    the track as the master finishes it (24-bit)
//   MIX BEFORE THE LIMITER.wav    the same with the master's last stage left off
//   MIX BEFORE THE MASTER.wav     the mix as the master is given it: the stems added together
//   STEMS/                        what adds up to that, to the sample: each lane as it goes into the mix,
//                                 the two returns, the transition, and the lanes that play through the drum
//                                 bus as one file, as the bus leaves them
//   INTO THE DRUM BUS/            those lanes one by one, as they go into it
// A lane that goes through a bus that did nothing, from first sample to last, is a stem of its own.
struct Session::Print : Engine::RecSink
{
    static constexpr int kBlock = 128;
    // One file: what it is given, less what is left out at its start. One that is `lazy` is made when it is first
    // given a sound (most lanes go into the mix one way only), with silence for what it was not given before.
    struct File
    {
        juce::File file;
        std::unique_ptr<juce::AudioFormatWriter> w;
        int bits = 32;
        bool lazy = true;
        juce::int64 drop = 0, given = 0;
        float peak = 0.0f;
    };
    enum { fSet, fPre, fMix, fDirect, fBus = fDirect + kLanes, fBusOut = fBus + kLanes, fRet, fTrans = fRet + 2, kFiles };
    std::array<File, kFiles> f;
    std::unique_ptr<Engine> eng;
    std::unique_ptr<Session> ses;
    juce::File dir;
    float deltaPeak = 0.0f;                             // what the drum bus did, at its most (under a hundred-thousandth: nothing)
    bool failed = false;
    sf::LoudnessMeter loud, loudPre, loudMix;
    float blockPeak = 0.0f;                             // the set's, this block
    juce::AudioBuffer<float> buf { 2, kBlock };
    std::array<float, 1024> tl {}, tr {}, zero {};
    std::vector<double> barSum, barMax;                 // (printTimeOnly: by the bar, the seconds its blocks took and the longest of them against its length)
    std::vector<int> barBlocks;

    enum Phase { play, tail } phase = play;
    juce::int64 total = 0, played = 0, tailed = 0, quiet = 0;
    double rate = 48000.0, tempo = 132.0;
    juce::Time began;

    juce::int64 written() const { return f[fSet].given; }       // samples of the set written

    bool open (File& o)
    {
        o.file.getParentDirectory().createDirectory();
        juce::WavAudioFormat wav;
        if (auto stream = o.file.createOutputStream())
        {
            o.w.reset (wav.createWriterFor (stream.get(), rate, 2, o.bits, {}, 0));
            if (o.w != nullptr) stream.release();
        }
        if (o.w == nullptr) { failed = true; return false; }
        juce::AudioBuffer<float> z (2, 4096);
        z.clear();
        for (juce::int64 left = o.given; left > 0 && ! failed; left -= 4096)
            if (! o.w->writeFromAudioSampleBuffer (z, 0, (int) std::min ((juce::int64) 4096, left))) failed = true;
        return ! failed;
    }
    // (gives back the first of the n that was kept: n, if none was)
    int put (int which, const float* l, const float* r, int n) noexcept
    {
        auto& o = f[(size_t) which];
        int from = 0;
        if (o.drop > 0) { from = (int) std::min ((juce::int64) n, o.drop); o.drop -= from; }
        if (from >= n) return n;
        float pk = 0.0f;
        for (int i = from; i < n; ++i) pk = std::max (pk, std::max (std::abs (l[i]), std::abs (r[i])));
        if (o.w == nullptr && (! o.lazy || pk > 1.0e-7f)) open (o);
        if (o.w != nullptr)
        {
            const float* ch[2] = { l + from, r + from };
            if (! o.w->writeFromFloatArrays (ch, 2, n - from)) failed = true;
        }
        o.given += n - from;
        o.peak = std::max (o.peak, pk);
        return from;
    }
    // a lane's buffer by a gain ramped across it, as the engine adds it to the mix or to the bus
    void ramped (int which, const float* l, const float* r, int n, float was, float now) noexcept
    {
        for (int at = 0; at < n; at += (int) tl.size())
        {
            const int m = std::min ((int) tl.size(), n - at);
            if (was == 1.0f && now == 1.0f) { put (which, l + at, r + at, m); continue; }
            if (was == 0.0f && now == 0.0f) { put (which, zero.data(), zero.data(), m); continue; }
            const float inc = (now - was) / (float) n;
            float g = was + inc * (float) at;
            for (int i = 0; i < m; ++i) { tl[(size_t) i] = g * l[at + i]; tr[(size_t) i] = g * r[at + i]; g += inc; }
            put (which, tl.data(), tr.data(), m);
        }
    }

    void begin (int) noexcept override { blockPeak = 0.0f; }
    void end (int) noexcept override {}
    bool wantsMore() const noexcept override { return true; }
    void track (int t, const float* l, const float* r, int n) noexcept override
    {
        if (t == 0)
        {
            for (int i = 0; i < n; ++i) blockPeak = std::max (blockPeak, std::max (std::abs (l[i]), std::abs (r[i])));
            for (int i = put (fSet, l, r, n); i < n; ++i) loud.push (l[i], r[i]);
        }
        else if (t == kLanes + 1 || t == kLanes + 2) put (fRet + (t - kLanes - 1), l, r, n);
        else if (t == kLanes + 3) { for (int i = 0; i < n; ++i) deltaPeak = std::max (deltaPeak, std::max (std::abs (l[i]), std::abs (r[i]))); }
        else if (t == kLanes + 4) put (fTrans, l, r, n);
        // (1 to kLanes, a lane whole: kept as its two ways into the mix, below)
    }
    void lanePaths (int lane, const float* l, const float* r, int n, float directWas, float directNow, float busWas, float busNow) noexcept override
    {
        if (lane < 0 || lane >= kLanes) return;
        ramped (fDirect + lane, l, r, n, directWas, directNow);
        ramped (fBus + lane, l, r, n, busWas, busNow);
    }
    void also (int which, const float* l, const float* r, int n) noexcept override
    {
        if (which == kBusOut) put (fBusOut, l, r, n);
        else if (which == kMasterIn) { for (int i = put (fMix, l, r, n); i < n; ++i) loudMix.push (l[i], r[i]); }
        else if (which == kLimiterIn) { for (int i = put (fPre, l, r, n); i < n; ++i) loudPre.push (l[i], r[i]); }
    }
};

float Session::printDone() const
{
    if (print == nullptr) return 0.0f;
    if (print->phase == Print::tail) return 0.99f;
    return (float) juce::jlimit (0.0, 0.98, (double) print->played / (double) std::max ((juce::int64) 1, print->total));
}

static String hmsOf (double seconds)
{
    const auto s = (juce::int64) std::llround (seconds);
    return String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
}

String Session::printBegin (const juce::File& root)
{
    if (print != nullptr) return "IT IS PRINTING";
    if (arrangement.empty()) return "NOTHING IN THE ARRANGEMENT";
    if (engine.hasRecSink()) return "A TAKE IS BEING RECORDED: END IT FIRST";
    if (! engine.isPrepared()) return "THE AUDIO HAS NOT STARTED";
    const juce::File into = root.isDirectory() || root.createDirectory() ? root : juce::File();
    if (into == juce::File()) return "CANNOT MAKE " + root.getFullPathName().toUpperCase();
    const auto began = juce::Time::getCurrentTime();
    juce::File dir = into.getChildFile ("PRINT " + began.formatted ("%Y-%m-%d %H.%M.%S"));
    for (int n = 2; dir.exists(); ++n) dir = into.getChildFile ("PRINT " + began.formatted ("%Y-%m-%d %H.%M.%S") + " " + String (n));
    if (! dir.createDirectory()) return "CANNOT MAKE " + dir.getFullPathName().toUpperCase();

    if (arrOn) arrStop();
    arrEdit = -1;                                       // (an EDIT under way is dropped)
    if (audioPause) audioPause();                       // one rig sounds at a time: this one waits
    sf::Bus::renderClockBegin();                        // (and the bus goes by the print's own time, not the wall's: DECISIONS 370)
    auto p = std::make_shared<Print>();
    p->dir = dir; p->began = began;
    p->rate = engine.sampleRate(); p->tempo = engine.tempo();
    // its own rig, as this one stands
    p->eng = std::make_unique<Engine>();
    p->eng->prepare (p->rate, Print::kBlock);
    p->eng->setOffline (true);
    p->ses = std::make_unique<Session> (*p->eng, false);
    p->ses->printRig = true;
    p->ses->takeState (stateVar());
    p->eng->setRunning (false);
    p->ses->arrRecall (0, 0.0);                         // its first part, there at once
    print = p;
    // The files. The set is held back by the lanes' wait and the master chain's; the mix before the limiter by the lanes'
    // and the master's stages before it; the rest by the lanes' alone: each file leaves out its own, and all begin on bar 1.
    const juce::File stems = dir.getChildFile ("STEMS"), intoBus = dir.getChildFile ("INTO THE DRUM BUS");
    const juce::int64 lanesWait = p->eng->laneLatency();
    for (auto& o : p->f) o.drop = lanesWait;
    p->f[Print::fSet].file = dir.getChildFile (Recorder::trackName (0) + ".wav"); p->f[Print::fSet].bits = 24; p->f[Print::fSet].lazy = false; p->f[Print::fSet].drop += p->eng->masterLatency();
    p->f[Print::fPre].file = dir.getChildFile ("MIX BEFORE THE LIMITER.wav"); p->f[Print::fPre].lazy = false; p->f[Print::fPre].drop += p->eng->masterLatencyBeforeLast();
    p->f[Print::fMix].file = dir.getChildFile ("MIX BEFORE THE MASTER.wav"); p->f[Print::fMix].lazy = false;
    for (int l = 0; l < kLanes; ++l)
    {
        p->f[(size_t) (Print::fDirect + l)].file = stems.getChildFile (Recorder::trackName (1 + l) + ".wav");
        p->f[(size_t) (Print::fBus + l)].file = intoBus.getChildFile (Recorder::trackName (1 + l) + ".wav");
    }
    p->f[Print::fBusOut].file = stems.getChildFile (Recorder::trackName (kLanes + 3) + ".wav");
    for (int k = 0; k < 2; ++k) p->f[(size_t) (Print::fRet + k)].file = stems.getChildFile (Recorder::trackName (kLanes + 1 + k) + ".wav");
    p->f[Print::fTrans].file = stems.getChildFile (Recorder::trackName (kLanes + 4) + ".wav");
    if (! printTimeOnly)                                // (timed only: nothing is written)
    for (int which : { (int) Print::fSet, (int) Print::fPre, (int) Print::fMix })
        if (! p->open (p->f[(size_t) which])) { printEnd (false, "CANNOT WRITE IN " + dir.getFullPathName().toUpperCase()); return printNote; }
    p->loudPre.prepare (p->rate); p->loudMix.prepare (p->rate);
    p->loud.prepare (p->rate);
    p->total = (juce::int64) std::llround ((double) arrBars() * 4.0 * 60.0 / p->tempo * p->rate);
    p->eng->setRecSink (printTimeOnly ? nullptr : p.get());
    p->ses->arrOn = true; p->ses->arrAt = 0; p->ses->arrZero = 0.0; p->ses->arrWait = false;
    p->ses->arrBreakFor = -1; p->ses->arrBreakAhead();         // (a BREAK at the top: there from the first beat)
    p->eng->setRunning (true);
    printNote = "PRINTING";
    printFolder = dir;
    if (onChange) onChange();
    return {};
}

bool Session::printStep (double msToSpend)
{
    if (print == nullptr) return false;
    auto& p = *print;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto render = [&p] (int n)
    {
        p.buf.setSize (2, n, false, false, true);
        if (! printTimeOnly) p.eng->render (p.buf);
        else
        {
            const juce::int64 a = juce::Time::getHighResolutionTicks();
            p.eng->render (p.buf);
            const double took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - a) / ((double) n / p.rate);
            const size_t bar = (size_t) ((double) p.played / (p.rate * 240.0 / p.tempo));
            if (p.barSum.size() <= bar) { p.barSum.resize (bar + 1, 0.0); p.barMax.resize (bar + 1, 0.0); p.barBlocks.resize (bar + 1, 0); }
            p.barSum[bar] += took; p.barMax[bar] = std::max (p.barMax[bar], took); ++p.barBlocks[bar];
        }
        sf::Bus::renderClockAdvance ((double) n / p.rate);
    };
    while (juce::Time::getMillisecondCounterHiRes() - t0 < msToSpend)
    {
        if (p.failed) { printEnd (false, "THE DISK STOPPED TAKING IT"); return false; }
        if (p.phase == Print::play)
        {
            // to the sample its last bar ends on, and not one further
            const int n = (int) std::min ((juce::int64) Print::kBlock, p.total - p.played);
            if (n > 0) { render (n); p.played += n; p.ses->arrTick(); }
            if (p.played >= p.total && printTimeOnly)
            {
                printTimes.clear();
                for (size_t b = 0; b < p.barSum.size(); ++b) printTimes.push_back ({ (float) (p.barSum[b] / std::max (1, p.barBlocks[b])), (float) p.barMax[b] });
                printEnd (false, "TIMED");
                return false;
            }
            if (p.played >= p.total)
            {
                p.ses->arrOn = false; p.ses->arrAt = -1;
                p.eng->setRunning (false);
                p.phase = Print::tail; p.quiet = 0;
            }
        }
        else
        {
            // what rings on after the last bar: until it has been silent for half a second (a minute at the most)
            render (Print::kBlock);
            p.tailed += Print::kBlock;
            p.quiet = p.blockPeak < 3.2e-5f ? p.quiet + Print::kBlock : 0;       // (-90 dB)
            const juce::int64 held = p.eng->laneLatency() + p.eng->masterLatency();     // (what the rig was still holding back has to come out, at the least)
            if ((p.tailed > held && p.quiet >= (juce::int64) (0.5 * p.rate)) || p.tailed >= (juce::int64) (60.0 * p.rate)) { printEnd (true, {}); return false; }
        }
    }
    return true;
}

void Session::printCancel() { if (print != nullptr) printEnd (false, "PRINT STOPPED"); }

// One sound file added into another of the same length and kind (a lane that went into the mix both ways, through a bus
// that did nothing: its two files are one stem). False if it could not be done; the first is then as it was.
static bool addInto (const juce::File& into, const juce::File& from)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> a (wav.createReaderFor (into.createInputStream().release(), true)), b (wav.createReaderFor (from.createInputStream().release(), true));
    if (a == nullptr || b == nullptr || a->lengthInSamples != b->lengthInSamples) return false;
    const juce::File tmp = into.getSiblingFile (into.getFileNameWithoutExtension() + ".adding.wav");
    tmp.deleteFile();
    bool ok = false;
    if (auto stream = tmp.createOutputStream())
        if (std::unique_ptr<juce::AudioFormatWriter> w { wav.createWriterFor (stream.get(), a->sampleRate, 2, (int) a->bitsPerSample, {}, 0) })
        {
            stream.release();
            ok = true;
            juce::AudioBuffer<float> x (2, 65536), y (2, 65536);
            for (juce::int64 at = 0; at < a->lengthInSamples && ok; at += 65536)
            {
                const int n = (int) std::min ((juce::int64) 65536, a->lengthInSamples - at);
                a->read (&x, 0, n, at, true, true); b->read (&y, 0, n, at, true, true);
                for (int ch = 0; ch < 2; ++ch) x.addFrom (ch, 0, y, ch, 0, n);
                ok = w->writeFromAudioSampleBuffer (x, 0, n);
            }
        }
    a = nullptr; b = nullptr;
    if (! ok) { tmp.deleteFile(); return false; }
    return tmp.moveFileTo (into);
}

void Session::printEnd (bool keep, const String& why)
{
    if (print == nullptr) return;
    auto p = print;
    p->eng->setRecSink (nullptr);
    p->eng->setRunning (false);
    sf::Bus::renderClockEnd();
    if (keep)
    {
        // (the set began the master chain's wait later than its parts, and the mix before the limiter part of that wait
        // later: that much silence at the end of each, and all the files are one length)
        auto pad = [&p] (int which, int n)
        {
            auto& o = p->f[(size_t) which];
            if (o.w == nullptr || n <= 0) return;
            juce::AudioBuffer<float> z (2, n);
            z.clear();
            o.w->writeFromAudioSampleBuffer (z, 0, n);
        };
        pad (Print::fSet, p->eng->masterLatency()); pad (Print::fPre, p->eng->masterLatencyBeforeLast());
    }
    for (auto& o : p->f) o.w = nullptr;                 // lengths written, files closed
    if (! keep) { p->dir.deleteRecursively(); printNote = why; }
    else
    {
        auto& f = p->f;
        auto heard = [&f] (int which) { return f[(size_t) which].peak >= 1.0e-5f; };
        const juce::File intoBus = p->dir.getChildFile ("INTO THE DRUM BUS");
        // The drum bus did nothing, first sample to last (at rest, or nothing played through it): the lanes its CONFIG
        // names are stems of their own, and there is no file of the bus's.
        const bool busWorked = p->deltaPeak >= 1.0e-5f && heard (Print::fBusOut);
        printJoined = 0;
        if (! busWorked)
        {
            for (int l = 0; l < kLanes; ++l)
            {
                auto& d = f[(size_t) (Print::fDirect + l)]; auto& b = f[(size_t) (Print::fBus + l)];
                if (! b.file.existsAsFile()) continue;
                if (heard (Print::fBus + l))
                {
                    if (d.file.existsAsFile() && heard (Print::fDirect + l)) { if (! addInto (d.file, b.file)) continue; ++printJoined; }      // (it could not be added: left where it is, and said)
                    else { d.file.deleteFile(); d.file.getParentDirectory().createDirectory(); if (! b.file.moveFileTo (d.file)) continue; }
                    d.peak = std::max (d.peak, b.peak);
                }
                b.file.deleteFile(); b.peak = 0.0f;
            }
            f[Print::fBusOut].file.deleteFile(); f[Print::fBusOut].peak = 0.0f;
        }
        // what was silent throughout is not kept
        juce::StringArray silent;
        for (int which = Print::fDirect; which < Print::kFiles; ++which)
            if (! heard (which)) f[(size_t) which].file.deleteFile();
        for (int l = 0; l < kLanes; ++l) if (! heard (Print::fDirect + l) && ! heard (Print::fBus + l)) silent.add (Recorder::trackName (1 + l));
        for (int k = 0; k < 2; ++k) if (! heard (Print::fRet + k)) silent.add (Recorder::trackName (kLanes + 1 + k));
        if (! heard (Print::fTrans)) silent.add (Recorder::trackName (kLanes + 4));
        if (intoBus.isDirectory() && intoBus.getNumberOfChildFiles (juce::File::findFiles) == 0) intoBus.deleteRecursively();

        const double len = (double) p->written() / p->rate, music = (double) p->total / p->rate;
        auto dbOf = [] (float peak) { return 20.0f * std::log10 (std::max (1.0e-9f, peak)); };
        const float pk = dbOf (f[Print::fSet].peak);
        auto line = [&] (const String& name, const String& what, int which, const sf::LoudnessMeter* m)
        {
            String s = (name + "  ").paddedRight (' ', 30) + what;
            s = (s + "  ").paddedRight (' ', 98) + (m != nullptr ? String (m->integrated, 1).paddedLeft (' ', 6) + " LUFS  " : String ("             ")) + "peak " + String (dbOf (f[(size_t) which].peak), 1).paddedLeft (' ', 6) + " dBFS\n";
            return s;
        };
        String n;
        n << "SF-000 PRINT\n"
          << "MADE     " << p->began.formatted ("%Y-%m-%d %H:%M:%S") << "\n"
          << "LENGTH   " << hmsOf (len) << "  (" << String (p->written()) << " samples at " << String (p->rate, 0) << " Hz: " << String (arrBars()) << " bars at " << String (p->tempo, 1)
          << ", " << hmsOf (music) << ", and " << String (len - music, 1) << " s ringing on after the last)\n"
          << "LOUDNESS " << String (p->loud.integrated, 1) << " LUFS integrated, the highest sample at " << String (pk, 1) << " dBFS\n\n"
          << "THE ARRANGEMENT\n";
        for (int i = 0; i < (int) arrangement.size(); ++i)
        {
            const auto& part = arrangement[(size_t) i];
            const auto* sn = findSnap (part.snap);
            const int bar = arrBarsBefore (i);
            n << String (i + 1).paddedLeft (' ', 3) << "  BAR " << String (bar + 1).paddedLeft (' ', 4) << "  " << hmsOf ((double) bar * 4.0 * 60.0 / p->tempo).paddedLeft (' ', 6)
              << "  " << String (part.bars).paddedLeft (' ', 3) << (part.bars == 1 ? " BAR   " : " BARS  ") << (sn != nullptr ? sn->name : String ("(GONE)"))
              << (i == 0 ? String() : arrMorphBars (i) <= 0.0 ? String ("   CUT") : "   MORPH " + (part.morph > 0 && arrMorphBars (i) >= (double) part.bars && glideBarsFor ((float) part.morph / 11.0f) > (double) part.bars ? String ("ALL OF IT") : morphName (part.morph)))
              << "\n";
        }
        // The files, said so that someone who was not there can use them: a mastering engineer.
        juce::StringArray onBus;
        for (int l = 0; l < kLanes; ++l) if (heard (Print::fBus + l)) onBus.add (lanes()[(size_t) l].name);
        n << "\nTHE FILES\n"
          << "Stereo WAV at " << String (p->rate, 0) << " Hz, all one length, bar 1 on the first sample of each. Nothing is normalised,\n"
          << "dithered or cut flat. The 32-bit files are floating point: one may go over full scale, and is whole where it does.\n\n"
          << line ("00 SET.wav", "the track as SF-000's master finishes it, limiter and all. 24-bit", Print::fSet, &p->loud)
          << line ("MIX BEFORE THE LIMITER.wav", "the same without the limiter, at the level it reached it. 32-bit", Print::fPre, &p->loudPre)
          << line ("MIX BEFORE THE MASTER.wav", "none of the master on it: the stems added together. 32-bit", Print::fMix, &p->loudMix);
        // (one of the two over full scale: whole, as a floating point file is, and said, for whoever opens it in something that is not)
        for (int which : { (int) Print::fPre, (int) Print::fMix })
            if (f[(size_t) which].peak > 1.0f)
                n << f[(size_t) which].file.getFileName() << " goes over full scale, by " << String (dbOf (f[(size_t) which].peak), 1) << " dB at its highest. The file is whole; turn it down by that or more\n"
                  << "before it meets anything that is not floating point.\n";
        n << "\nSTEMS/  add up to MIX BEFORE THE MASTER, to the sample. 32-bit.\n";
        for (int l = 0; l < kLanes; ++l) if (heard (Print::fDirect + l)) n << line ("  " + Recorder::trackName (1 + l) + ".wav", "the lane as it goes into the mix", Print::fDirect + l, nullptr);
        if (heard (Print::fRet)) n << line ("  " + Recorder::trackName (kLanes + 1) + ".wav", "return A (the delay), as it goes into the mix", Print::fRet, nullptr);
        if (heard (Print::fRet + 1)) n << line ("  " + Recorder::trackName (kLanes + 2) + ".wav", "return B (the reverb), as it goes into the mix", Print::fRet + 1, nullptr);
        if (heard (Print::fBusOut)) n << line ("  " + Recorder::trackName (kLanes + 3) + ".wav", "the lanes on the drum bus, together, as the bus leaves them", Print::fBusOut, nullptr);
        if (heard (Print::fTrans)) n << line ("  " + Recorder::trackName (kLanes + 4) + ".wav", "the transitions: lead-ins and hits", Print::fTrans, nullptr);
        if (onBus.size() > 0)
        {
            n << "\nINTO THE DRUM BUS/  the lanes inside " << Recorder::trackName (kLanes + 3) << ".wav (" << onBus.joinIntoString (", ") << "), one by one, as they go into the bus.\n"
              << "Not to be added to the stems: they are in them already. For when the drums are wanted apart, without what the bus did.\n";
            for (int l = 0; l < kLanes; ++l) if (heard (Print::fBus + l)) n << line ("  " + Recorder::trackName (1 + l) + ".wav", "", Print::fBus + l, nullptr);
        }
        if (silent.size() > 0) n << "\nSilent throughout, and not kept: " << silent.joinIntoString (", ") << ".\n";
        n << "\nA lane's file is the lane as it leaves its strip: after its effects, its EQ and its level, and after POCKETS has\n"
          << "cut it where another lane had the better claim to that part of the spectrum. What it sends to the two returns is in\n"
          << "the returns' files. None of the master is on a stem: not its low end (the lows made mono, a rumble cut), its tape,\n"
          << "its compressor, FINISH or its limiter.\n"
          << "For mastering: MIX BEFORE THE LIMITER, or the STEMS, with 00 SET to hear what was meant.\n\n"
          << "It was rendered, not recorded: on a rig made for the purpose from the session as it stood, every\n"
          << "module from its beginning, so the same arrangement prints the same files. Nothing was played\n"
          << "into it from outside, and each part brought its own notes. The snapshot knobs stood where they\n"
          << "were, and MOTION ran.\n";
        p->dir.getChildFile ("PRINT.txt").replaceWithText (n);
        printNote = "PRINTED: " + hmsOf (len) + ", " + String (p->loud.integrated, 1) + " LUFS, PEAK " + String (pk, 1) + " DB.  " + p->dir.getFullPathName().toUpperCase();
    }
    print = nullptr;
    p->ses = nullptr; p->eng = nullptr;                 // its rig is let go before this one sounds again
    if (audioResume) audioResume();
    if (onChange) onChange();
}
} // namespace sf0
