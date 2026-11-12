#pragma once
// What the screen is made of, as solid things: the plates, knobs, keys, panes and lamps the lit
// skin draws as pictures, written down instead with where they are (screen units) and what state
// they are in. The skin records them from one whole drawing of the screen; the 3D stage builds
// the instrument from the record, and finds what the pointer is on in it.
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <vector>

namespace sf0
{
struct Parts
{
    using R = juce::Rectangle<float>;
    // Heights, in screen units (5.46 to the model's millimetre), above whatever a thing stands on.
    static constexpr float kU = 5.46f;
    static constexpr float kPlate = 1.27f * kU;         // a plate's top above the case's
    static constexpr float kPane = 0.51f * kU;          // a display's glass above its plate
    static constexpr float kKey = 1.27f * kU, kKeyDown = 0.4f * kU;
    static constexpr float kCapBig = 27.74f, kCapSmall = 19.82f;
    static constexpr float kCapRBig = 11.84f, kCapRSmall = 8.29f;      // a cap at its widest
    static constexpr float kCase = 25.4f * kU;          // how deep the case is

    // What is cut off (the rows where they pass out of sight): nothing of the part shows outside it.
    struct Clip { float x0 = -1.0e9f, y0 = -1.0e9f, x1 = 1.0e9f, y1 = 1.0e9f;
                  bool has (float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; } };

    struct Plate  { R r; juce::uint32 ground = 0; Clip clip; float base = 0.0f; };
    // ctl: the engine's control it shows (its lights then follow the engine by themselves), or -1
    // and a, b, mark say what is lit (turns from six o'clock; mark: where it is going, or below 0).
    struct Knob   { float cx = 0, cy = 0; bool big = true; int ctl = -1, kind = 0, steps = 0; juce::uint32 light = 0; float a = 0, b = 0, mark = -1.0f; Clip clip; float base = 0.0f; };
    struct Key    { R r; juce::uint32 colour = 0; bool down = false, glows = false; Clip clip; float base = 0.0f; };
    struct Lamp   { float cx = 0, cy = 0, d = 12.0f; juce::uint32 colour = 0; bool on = false; Clip clip; float base = 0.0f; };
    struct Hollow { float cx = 0, cy = 0, d = 40.0f; bool round = true; Clip clip; float base = 0.0f; };
    struct Pane   { R r; juce::uint32 glass = 0; Clip clip; float base = 0.0f; };
    struct Badge  { R r; float base = 0.0f; };

    std::vector<Plate> plates;
    std::vector<Knob> knobs;
    std::vector<Key> keys;
    std::vector<Lamp> lamps;
    std::vector<Hollow> hollows;
    std::vector<Pane> panes;
    std::vector<Badge> badges;
    bool body = false, dark = false;

    void clear() { plates.clear(); knobs.clear(); keys.clear(); lamps.clear(); hollows.clear(); panes.clear(); badges.clear(); body = false; }

    // What each thing stands on: the case (0), or the top of the plate it is over. A plate wholly
    // inside an earlier one (a card on a panel) stands on that one.
    void settle()
    {
        for (size_t i = 0; i < plates.size(); ++i)
        {
            plates[i].base = 0.0f;
            for (size_t j = 0; j < i; ++j)
                if (plates[j].r.contains (plates[i].r) && overlap (plates[i].clip, plates[j].clip))
                    plates[i].base = std::max (plates[i].base, plates[j].base + kPlate);
        }
        auto on = [this] (float x, float y)
        {
            float top = 0.0f;
            for (const auto& p : plates) if (p.r.contains (x, y) && p.clip.has (x, y)) top = std::max (top, p.base + kPlate);
            return top;
        };
        for (auto& k : knobs) k.base = on (k.cx, k.cy);
        for (auto& k : keys) k.base = on (k.r.getCentreX(), k.r.getCentreY());
        for (auto& l : lamps) l.base = on (l.cx, l.cy);
        for (auto& h : hollows) h.base = on (h.cx, h.cy);
        for (auto& p : panes) p.base = on (p.r.getCentreX(), p.r.getCentreY());
        for (auto& b : badges) b.base = on (b.r.getCentreX(), b.r.getCentreY());
    }

    // A number that changes when anything that casts a shadow has moved (not when a light has).
    juce::uint64 shape() const
    {
        juce::uint64 h = 1469598103934665603ull;
        auto mix = [&h] (float v) { juce::uint32 u; std::memcpy (&u, &v, 4); h = (h ^ u) * 1099511628211ull; };
        auto rect = [&mix] (R r) { mix (r.getX()); mix (r.getY()); mix (r.getWidth()); mix (r.getHeight()); };
        auto clip = [&mix] (const Clip& c) { mix (c.x0); mix (c.y0); mix (c.x1); mix (c.y1); };
        for (const auto& p : plates) { rect (p.r); clip (p.clip); }
        for (const auto& k : knobs) { mix (k.cx); mix (k.cy); mix (k.big ? 1.0f : 2.0f); clip (k.clip); }
        for (const auto& k : keys) { rect (k.r); mix (k.down ? 1.0f : 2.0f); clip (k.clip); }
        for (const auto& p : panes) { rect (p.r); clip (p.clip); }
        for (const auto& b : badges) rect (b.r);
        mix (body ? 1.0f : 2.0f);
        return h;
    }

private:
    static bool overlap (const Clip& a, const Clip& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
};
} // namespace sf0
