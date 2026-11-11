#include "Session.h"

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
    if (useDisk) startTimer (3000);
}

Session::~Session()
{
    stopTimer();
    if (useDisk) save();
}

void Session::timerCallback()
{
    if (dirty) save();
}

double Session::glideBeats() const { return glideBarsFor (engine.target (cx::kGlide)) * 4.0; }

// ------------------------------------------------------------------------------------- controls
void Session::userSet (int i, float v)
{
    if (i < 0 || i >= cx::kCount) return;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (i >= cx::kSnap) { turnSnapKnob (i - cx::kSnap, v); return; }
    const auto& c = engine.control (i);
    base[(size_t) i] = c.morph && i < kMorphEnd ? v - offset[(size_t) i] : v;
    engine.set (i, v);
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
    return s;
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
    library.push_back (s);
    touch();
    return s.id;
}

void Session::applySnapshot (const Snapshot& s, double glide)
{
    // What cannot travel goes at once: menus, switches, insert cells, generator knobs, the key.
    for (int i = 0; i < cx::kCount && i < (int) s.values.size(); ++i)
    {
        const float v = s.values[(size_t) i];
        if (v < 0.0f) continue;
        const bool morph = engine.control (i).morph && i < kMorphEnd;
        base[(size_t) i] = v;
        if (! morph) engine.set (i, v);
    }
    for (int l = 0; l < kLanes; ++l)
    {
        for (int k = 0; k < kSlots; ++k)
        {
            // An insert comes back whole: the effect, its CONFIG, and the files it held. One that
            // is already there as the snapshot has it is left running.
            const int type = s.inserts[(size_t) l][(size_t) k];
            const auto& refs = s.insFiles[(size_t) l][(size_t) k];
            const bool asItIs = s.hasInsFiles ? engine.insertHolds (l, k, type, refs) : engine.insertType (l, k) == type;
            if (asItIs) continue;
            if (s.hasInsFiles && type >= 0) engine.expectInsertFiles (l, k, refs);
            engine.setInsertType (l, k, type, false, s.hasSetup ? &s.insSetup[(size_t) l][(size_t) k] : nullptr);
        }
        engine.setPattern (l, s.patterns[(size_t) l]);
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
    for (auto& k : knobs) k.snaps.erase (std::remove (k.snaps.begin(), k.snaps.end(), id), k.snaps.end());
    if (baseId == id) baseId = 0;
    applyMorph (0.0);
    touch();
}

void Session::rename (juce::int64 id, const juce::String& name)
{
    const int i = indexOf (id);
    if (i >= 0 && name.trim().isNotEmpty()) { library[(size_t) i].name = name.trim().toUpperCase().substring (0, 24); touch(); }
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
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", s.id);
    o->setProperty ("name", s.name);
    o->setProperty ("time", s.time.toMilliseconds());
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

static Snapshot snapFromVar (const var& v, const Engine& e)
{
    Snapshot s;
    s.id = (juce::int64) v.getProperty ("id", 0);
    s.name = v.getProperty ("name", "SNAP").toString();
    s.time = juce::Time ((juce::int64) v.getProperty ("time", 0));
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
    auto* root = new juce::DynamicObject();
    root->setProperty ("sf000", 1);

    Snapshot b = capture();
    for (int i = 0; i < kMorphEnd; ++i)
        if (engine.control (i).morph && b.values[(size_t) i] >= 0.0f) b.values[(size_t) i] = juce::jlimit (0.0f, 1.0f, base[(size_t) i]);
    b.name = "BASE";
    root->setProperty ("base", snapToVar (b, engine));
    root->setProperty ("baseId", baseId);

    auto* other = new juce::DynamicObject();        // what a snapshot leaves alone: tempo, glide, master, the snapshot knobs
    for (int i = 0; i < cx::kCount; ++i)
        if (engine.control (i).used && ! engine.control (i).snapshot) other->setProperty (engine.control (i).id, engine.target (i));
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
        kn.add (var (o));
    }
    root->setProperty ("library", lib);
    root->setProperty ("knobs", kn);

    const auto f = stateFile();
    f.getParentDirectory().createDirectory();
    const auto tmp = f.getSiblingFile ("session.tmp");
    if (tmp.replaceWithText (juce::JSON::toString (var (root)))) tmp.moveFileTo (f);
}

bool Session::load()
{
    const auto f = stateFile();
    if (! f.existsAsFile()) return false;
    const var root = juce::JSON::parse (f);
    if (! root.isObject() || (int) root.getProperty ("sf000", 0) != 1) return false;

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
        }
    if (auto* other = root.getProperty ("controls", var()).getDynamicObject())
        for (auto& p : other->getProperties())
        {
            const int i = engine.find (p.name.toString());
            if (i >= 0 && ! engine.control (i).snapshot) { engine.set (i, (float) p.value); base[(size_t) i] = engine.target (i); }
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
    if (at >= 0) { s.name = library[(size_t) at].name; s.time = library[(size_t) at].time; library[(size_t) at] = s; }
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
