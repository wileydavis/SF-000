#pragma once
// Where a plugin sits on the E16 (the scenes PERF and MIX, E16Map.h): the Live track its knobs
// are on, and the page its editor draws. One rule for GLIDE RINGS (the processor, audio thread: no allocation)
// and the editor's E16 grid.
#include "E16Map.h"
#include <cstring>

namespace sf::e16
{
// "REVERB|TERROIR" holds "REVERB" and "TERROIR" (a stand-in has the same knobs in the same places).
inline bool listHas (const char* list, const char* module)
{
    if (list == nullptr || module == nullptr || module[0] == 0) return false;
    const size_t n = std::strlen (module);
    for (const char* p = list; *p != 0;)
    {
        const char* bar = std::strchr (p, '|');
        const size_t len = bar != nullptr ? (size_t) (bar - p) : std::strlen (p);
        if (len == n && std::strncmp (p, module, n) == 0) return true;
        if (bar == nullptr) break;
        p = bar + 1;
    }
    return false;
}

// The scene's track for a plugin: the plugin's own Live track if the scene has the plugin there,
// else the one track the scene has it on (no name needed); "" if the scene has it on several tracks
// and the name doesn't say which (an AU never knows its track's name); nullptr: not on the scene.
inline const char* trackFor (const char* module, const char* trackName)
{
    const char* track = nullptr;
    for (const auto& k : kE16Knobs)
        if (std::strcmp (module, k.module) == 0)
        {
            if (trackName != nullptr && std::strcmp (trackName, k.track) == 0) return k.track;
            if (track == nullptr) track = k.track;
            else if (std::strcmp (track, k.track) != 0) track = "";
        }
    return track;
}

// A turn or press belongs to the plugin on `track` (track "": any of its tracks, when the name is
// unknown).
inline bool turnIs (const E16Turn& t, const char* module, const char* track)
{
    return t.abbr != nullptr && t.param >= 0 && listHas (t.modules, module)
        && (track[0] == 0 || std::strcmp (t.track, track) == 0);
}
inline bool pressIs (const E16Press& p, const char* module, const char* track)
{
    return p.name != nullptr && p.param >= 0 && listHas (p.modules, module)
        && (track[0] == 0 || std::strcmp (p.track, track) == 0);
}

inline constexpr int kNumPages = (int) (sizeof (kE16Pages) / sizeof (kE16Pages[0]));

// The plugin on `track` is on this encoder: a turn, the turn its press swaps to, or its press.
inline bool cellIs (const E16Cell& c, const char* module, const char* track)
{
    return turnIs (c.turn, module, track) || turnIs (c.second, module, track) || pressIs (c.press, module, track);
}

inline bool isShowPage (int pg) { return std::strcmp (kE16Pages[pg].scene, kE16ShowScene) == 0; }

// The page an editor shows: the one with most of the plugin's encoders; on a tie the last of them
// (what every PERF page has, NEXT SONG and BUILD, shows on NRG, the last).
inline int homePage (const char* module, const char* track)
{
    int best = 0, bestCount = 0;
    for (int pg = 0; pg < kNumPages; ++pg)
    {
        int n = 0;
        for (int e = 0; e < 16; ++e)
            n += cellIs (kE16Pages[pg].cells[e], module, track) ? 1 : 0;
        if (n > 0 && n >= bestCount) { best = pg; bestCount = n; }
    }
    return best;
}

// The plugin is on encoder e of every PERF page but `except`.
inline bool onEveryShowPage (int e, const char* module, const char* track, int except)
{
    int n = 0;
    for (int pg = 0; pg < kNumPages; ++pg)
        if (isShowPage (pg) && pg != except)
        {
            if (! cellIs (kE16Pages[pg].cells[e], module, track)) return false;
            ++n;
        }
    return n > 1;
}
// How a ring of the App's colour number c looks, as the plugins draw it (kE16Rings, from layout.py's
// RING_RGB): RGB. White for a number the table doesn't have.
inline unsigned ringRGB (int c)
{
    for (const auto& r : kE16Rings)
        if (r.colour == c) return r.rgb;
    return kE16Rings[0].rgb;
}
} // namespace sf::e16
