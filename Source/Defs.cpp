// SF-000 definitions: what each lane, effect and return of this build is, written out as plain
// data, with a print of each. See FORMAT.md.
#include "Engine.h"
#include "core/Processor.h"
#include "core/Registry.h"

namespace sf0
{
using var = juce::var;

// A print: 16 hex digits from the definition's text (FNV-1a, 64 bits). The same definition gives
// the same print on any machine; any change to it gives another.
static juce::String printOf (const var& def)
{
    const auto text = juce::JSON::toString (def, true, 6).toStdString();
    juce::uint64 h = 1469598103934665603ULL;
    for (const char c : text) { h ^= (juce::uint64) (unsigned char) c; h *= 1099511628211ULL; }
    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

static var knobVar (const KnobDef& k, const juce::String& id)
{
    if (k.param == nullptr) return {};
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("label", juce::String (k.label));
    o->setProperty ("param", juce::String (k.param));
    o->setProperty ("kind", juce::String::charToString ((juce::juce_wchar) k.kind));
    o->setProperty ("lo", k.lo);
    o->setProperty ("hi", k.hi);
    return var (o);
}

static var fixedVar (const std::vector<Fixed>& fixed)
{
    auto* o = new juce::DynamicObject();
    for (auto& f : fixed) o->setProperty (juce::String (f.param), f.value);
    return var (o);
}

static const char* kindName (Kind k)
{
    return k == Kind::Bipolar ? "bipolar" : k == Kind::List ? "list" : k == Kind::Toggle ? "toggle" : k == Kind::Hold ? "hold" : "knob";
}

const Engine::Defs& Engine::defs() const
{
    if (defsMade != nullptr) return *defsMade;
    auto d = std::make_unique<Defs>();
    auto add = [&d] (const var& def)
    {
        const auto print = printOf (def);
        for (auto& e : d->all) if (e.first == print) return print;
        d->all.push_back ({ print, def });
        return print;
    };

    // A sound engine: one of the suite's modules, with every parameter it has and that parameter's
    // range. A stored value from 0 to 1 is a place along a parameter's travel; this says what the
    // travel is.
    std::vector<std::pair<std::string, juce::String>> engines;
    auto engine = [&] (const char* module) -> juce::String
    {
        for (auto& e : engines) if (e.first == module) return e.second;
        SFProcessor p (sf::makeModule (module));
        auto* params = new juce::DynamicObject();
        for (auto& s : p.allSpecs())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", juce::String (s.name));
            o->setProperty ("type", s.type == sf::PType::Float ? "float" : s.type == sf::PType::Choice ? "choice" : s.type == sf::PType::Toggle ? "toggle" : "momentary");
            if (s.type == sf::PType::Float)
            {
                o->setProperty ("min", s.min);
                o->setProperty ("max", s.max);
                if (! std::isnan (s.centre) && s.centre > s.min && s.centre < s.max) o->setProperty ("centre", s.centre);
                if (! s.unit.empty()) o->setProperty ("unit", juce::String (s.unit));
            }
            if (s.type == sf::PType::Choice)
            {
                juce::Array<var> ch;
                for (auto& c : s.choices) ch.add (juce::String (c));
                o->setProperty ("choices", ch);
            }
            o->setProperty ("def", s.def);
            params->setProperty (juce::String (s.id), var (o));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", "engine");
        o->setProperty ("id", juce::String (module));
        o->setProperty ("params", var (params));
        engines.push_back ({ module, add (var (o)) });
        return engines.back().second;
    };

    const auto& L = lanes();
    for (int l = 0; l < kLanes; ++l)
    {
        const auto& ld = L[(size_t) l];
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", "lane");
        o->setProperty ("slot", juce::String (ld.slug));
        o->setProperty ("name", juce::String (ld.name));
        o->setProperty ("engineId", juce::String (ld.module));
        o->setProperty ("engine", engine (ld.module));
        o->setProperty ("pitched", ld.pitched);
        o->setProperty ("octave", ld.octave);
        o->setProperty ("fixed", fixedVar (ld.fixed));
        juce::Array<var> knobs;
        for (int k = 0; k < kLaneKnobs; ++k) knobs.add (knobVar (ld.knobs[k], juce::String (ld.slug) + "." + juce::String (ld.knobs[k].param)));
        o->setProperty ("knobs", knobs);
        d->slots.add (ld.slug);
        d->lane[(size_t) l] = add (var (o));
    }

    // An effect's cells are stored by place: <slot>.i<insert>.c<cell>. This says what each place is.
    for (auto& fd : effects())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", "fx");
        o->setProperty ("name", juce::String (fd.name));
        o->setProperty ("engineId", juce::String (fd.module));
        o->setProperty ("engine", engine (fd.module));
        o->setProperty ("fixed", fixedVar (fd.fixed));
        juce::Array<var> cells;
        for (int c = 0; c < kCells; ++c) cells.add (knobVar (fd.cells[c], "c" + juce::String (c + 1)));
        o->setProperty ("cells", cells);
        d->fx.push_back ({ juce::String (fd.name), add (var (o)) });
    }

    auto ret = [&] (const FxDef& fd, const char* prefix)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", "return");
        o->setProperty ("name", juce::String (fd.name));
        o->setProperty ("engineId", juce::String (fd.module));
        o->setProperty ("engine", engine (fd.module));
        o->setProperty ("fixed", fixedVar (fd.fixed));
        juce::Array<var> knobs;
        for (int k = 0; k < 4; ++k) knobs.add (knobVar (fd.cells[k], juce::String (prefix) + juce::String (fd.cells[k].param)));
        o->setProperty ("knobs", knobs);
        return add (var (o));
    };
    d->dub = ret (returnA(), "ret.dub_");
    d->space = ret (returnB (0), "ret.space_");
    d->conv = ret (returnB (1), "ret.conv_");

    // The chassis: what every lane has beside its instrument, and what is stored of the whole rig.
    {
        auto entry = [] (const Control& c, const juce::String& id)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("label", c.label);
            o->setProperty ("kind", kindName (c.kind));
            if (c.choices.size() > 0) { juce::Array<var> ch; for (auto& s : c.choices) ch.add (s); o->setProperty ("choices", ch); }
            return var (o);
        };
        juce::Array<var> strip, whole;
        const auto own = juce::String (L[0].slug) + ".";
        for (int k = kLaneKnobs; k < cx::kPerLane; ++k)
        {
            if (k >= cx::kIns && k < cx::kGen) continue;                        // an insert's cells: the effect's own
            const auto& c = controls[(size_t) cx::lane (0, k)];
            if (c.used && c.snapshot) strip.add (entry (c, c.id.fromFirstOccurrenceOf (own, false, false)));
        }
        for (int i = cx::kRetMS; i < cx::kCount; ++i)
            if (const auto& c = controls[(size_t) i]; c.used && c.snapshot) whole.add (entry (c, c.id));
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", "chassis");
        o->setProperty ("v", kChassisVersion);
        o->setProperty ("laneKnobs", kLaneKnobs);
        o->setProperty ("inserts", kSlots);
        o->setProperty ("cells", kCells);
        o->setProperty ("lane", strip);
        o->setProperty ("rig", whole);
        d->chassis = add (var (o));
    }

    defsMade = std::move (d);
    return *defsMade;
}
} // namespace sf0
