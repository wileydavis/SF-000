#include "Session.h"
#include "Build.h"

namespace sf0
{
using juce::var;

static const int kMorphEnd = cx::kRet + cx::kRetKnobs;      // lanes and returns: what a snapshot knob can reach

Session::Session (Engine& e, bool disk) : engine (e), useDisk (disk)
{
    genType.fill (-1);
    base.resize ((size_t) cx::kCount);
    offset.assign ((size_t) cx::kCount, 0.0f);
    for (int i = 0; i < cx::kCount; ++i) base[(size_t) i] = engine.target (i);
    for (int r = 0; r < 2; ++r) retInit[(size_t) r] = engine.getSetup ({ 2, 0, r });
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
    if (useDisk) save();
}

void Session::timerCallback()
{
    finishTakes();
    engine.tidy();
    // (the timer runs faster while a snapshot waits for its sound; the file is still written no more than every three seconds)
    if (const auto now = juce::Time::getMillisecondCounter(); useDisk && dirty && now - savedAt >= 2900) { savedAt = now; save(); }
    pruneCues();
    if (waiting.empty()) { if (useDisk) startTimer (3000); else stopTimer(); }
}

double Session::glideBeats() const { return glideBarsFor (engine.target (cx::kGlide)) * 4.0; }

// ------------------------------------------------------------------------------------- controls
void Session::selectLane (int l)
{
    l = juce::jlimit (0, kLanes - 1, l);
    if (l == activeLane) return;
    activeLane = l; selStep = -1; page = 0;
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
    engine.set (cx::kSnap + k, v);
    applyMorph (glideBeats());
    touch();
}

void Session::applyMorph (double glide)
{
    std::fill (offset.begin(), offset.end(), 0.0f);
    for (int k = 0; k < 16; ++k)
    {
        const auto& kn = knobs[(size_t) k];
        const float v = engine.target (cx::kSnap + k);
        std::vector<const Snapshot*> ss;
        for (auto id : kn.snaps) if (auto* s = findSnap (id)) ss.push_back (s);
        if (ss.size() < 2 || v <= 0.0f) continue;
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
}

void Session::pressSnapKnob (int k)
{
    const auto& kn = knobs[(size_t) k];
    std::vector<juce::int64> ids;
    for (auto id : kn.snaps) if (findSnap (id) != nullptr) ids.push_back (id);
    if (ids.empty()) return;
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
    std::vector<const Snapshot*> ss;
    for (auto id : knobs[(size_t) k].snaps) if (auto* s = findSnap (id)) ss.push_back (s);
    if (ss.size() < 2) return out;
    for (int i = 0; i < kMorphEnd; ++i)
    {
        if (! engine.control (i).morph) continue;
        float lo = 2.0f, hi = -2.0f; bool all = true;
        for (auto* s : ss) { const float v = s->values[(size_t) i]; if (v < 0.0f) { all = false; break; } lo = std::min (lo, v); hi = std::max (hi, v); }
        if (all && hi - lo > 1e-4f) out.push_back (i);
    }
    return out;
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
    s.hasSetup = true;
    s.hasInsFiles = true;
    s.made = madeNow (s);
    return s;
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
        else if (some && h.bars >= 4) { settle (w.id, h, true); any = true; }
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

// A lane's instrument is to change in a way that cannot travel: something CONFIG sets (its model,
// its layer, a sample's ends), or one of its knobs that is a menu or a switch.
bool Session::laneJumps (const Snapshot& s, int l) const
{
    if (s.hasSetup && setupDiffers (engine.getSetup ({ 0, l, 0 }), s.setup[(size_t) l])) return true;
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
    if (s.hasSetup && setupDiffers (engine.getSetup ({ 1, l, k }), s.insSetup[(size_t) l][(size_t) k])) return false;
    for (int c = 0; c < kCells; ++c)
    {
        const int i = cx::ins (l, k, c);
        const auto& ct = engine.control (i);
        const float v = i < (int) s.values.size() ? s.values[(size_t) i] : -1.0f;
        if (v >= 0.0f && ct.used && ! ct.glides && ct.kind != Kind::Hold && std::abs (v - engine.target (i)) > 1.0e-4f) return false;
    }
    return true;
}

void Session::applySnapshot (const Snapshot& s, double glide)
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
        engine.set (i, v, travels ? glide : 0.0);
    }
    for (int l = 0; l < kLanes; ++l)
    {
        engine.setPattern (l, s.patterns[(size_t) l], true);         // while the clock runs: on the next bar line
        genType[(size_t) l] = s.gen[(size_t) l];
        if (s.hasSetup)
        {
            engine.applySetup ({ 0, l, 0 }, s.setup[(size_t) l]);
            for (int k = 0; k < kSlots; ++k) engine.applySetup ({ 1, l, k }, s.insSetup[(size_t) l][(size_t) k]);
        }
    }
    if (s.hasSetup) for (int r = 0; r < 2; ++r) engine.applySetup ({ 2, 0, r }, s.retSetup[(size_t) r]);
    applyMorph (glide);
}
void Session::recall (juce::int64 id)
{
    if (auto* s = findSnap (id))
    {
        const Snapshot copy = *s;
        applySnapshot (copy, glideBeats());
        baseId = id;
        selStep = -1;
        touch();
        if (onChange) onChange();
    }
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
    if (baseId == id) baseId = 0;
    applyMorph (0.0);
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
juce::File Session::stateFile()
{
    if (stateFileForTests != juce::File()) return stateFileForTests;
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("SF-000").getChildFile ("session.json");
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
        for (auto& f : r.files) fl.add (f);
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
        o->setProperty ("setup", var (su));
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
    return s;
}

void Session::save()
{
    dirty = false;
    if (notSavingWhy.isNotEmpty()) return;          // a newer build's file is on the disk: it stays as it is
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
    juce::Array<var> fl;
    for (int l = 0; l < kLanes; ++l) fl.add (engine.laneFile (l));
    root->setProperty ("files", fl);
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
        for (auto& m : k.more) o->setProperty (m.name, m.value);
        kn.add (var (o));
    }
    root->setProperty ("library", lib);
    root->setProperty ("knobs", kn);
    // The definitions: every one the file has held, and this build's own. Never pruned: a snapshot
    // made long ago still finds what it was made with.
    {
        auto* d = new juce::DynamicObject();
        for (auto& e : defsKept) d->setProperty (e.first, e.second);
        for (auto& e : engine.defs().all) if (! d->hasProperty (e.first)) d->setProperty (e.first, e.second);
        root->setProperty ("defs", var (d));
    }
    for (auto& m : rootMore) if (! root->hasProperty (m.name)) root->setProperty (m.name, m.value);

    const auto f = stateFile();
    f.getParentDirectory().createDirectory();
    const auto tmp = f.getSiblingFile ("session.tmp");
    if (tmp.replaceWithText (juce::JSON::toString (var (root)))) tmp.moveFileTo (f);
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

    // What this build does not know is carried, not dropped: the next save writes it back.
    static const char* const known[] = { "sf000", "wrote", "libraryId", "defs", "base", "baseId", "controls", "activeLane", "files", "space", "insertFiles", "library", "knobs" };
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

    library.clear();
    if (auto* lib = root.getProperty ("library", var()).getArray())
        for (auto& v : *lib) library.push_back (snapFromVar (v, engine));
    if (auto* kn = root.getProperty ("knobs", var()).getArray())
        for (int k = 0; k < 16 && k < kn->size(); ++k)
        {
            const juce::var o = (*kn)[k];
            knobs[(size_t) k].name = o.getProperty ("name", "").toString();
            knobs[(size_t) k].depth = juce::jlimit (0.0f, 2.0f, (float) o.getProperty ("depth", 1.0));
            knobs[(size_t) k].snaps.clear();
            if (auto* ids = o.getProperty ("snaps", var()).getArray())
                for (auto& id : *ids) if (findSnap ((juce::int64) id) != nullptr) knobs[(size_t) k].snaps.push_back ((juce::int64) id);
            knobs[(size_t) k].more.clear();
            if (auto* ko = o.getDynamicObject())
                for (auto& p : ko->getProperties())
                    if (p.name != juce::Identifier ("name") && p.name != juce::Identifier ("depth") && p.name != juce::Identifier ("snaps")) knobs[(size_t) k].more.set (p.name, p.value);
        }
    if (auto* other = root.getProperty ("controls", var()).getDynamicObject())
        for (auto& p : other->getProperties())
        {
            const int i = engine.find (p.name.toString());
            if (i >= 0 && ! engine.control (i).snapshot) { engine.set (i, (float) p.value); base[(size_t) i] = engine.target (i); }
            if (i < 0) controlsMore.set (p.name, p.value);
        }
    activeLane = juce::jlimit (0, kLanes - 1, (int) root.getProperty ("activeLane", 7));
    if (const var fl = root.getProperty ("files", var()); fl.isArray())
        for (int l = 0; l < kLanes && l < fl.size(); ++l)
            if (const juce::File f (fl[l].toString()); fl[l].toString().isNotEmpty() && f.existsAsFile()) engine.loadFile (l, f);
    if (const var sp = root.getProperty ("space", var()); sp.isObject())
    {
        engine.setSpaceModel ((int) sp.getProperty ("model", 0));
        engine.setHeldFiles ({ 2, 0, 1 }, refsFromVar (sp.getProperty ("files", var())));
    }
    if (auto* insf = root.getProperty ("insertFiles", var()).getArray())
        for (auto& e : *insf)
        {
            const int l = (int) e.getProperty ("lane", -1), k = (int) e.getProperty ("slot", -1);
            if (l >= 0 && l < kLanes && k >= 0 && k < kSlots) engine.setHeldFiles ({ 1, l, k }, refsFromVar (e.getProperty ("files", var())));
        }
    baseId = (juce::int64) root.getProperty ("baseId", 0);
    selStep = -1;
    if (root.hasProperty ("base")) applySnapshot (snapFromVar (root["base"], engine), 0.0);
    warmInserts();
    return true;
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
    s.hasSetup = true;
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
