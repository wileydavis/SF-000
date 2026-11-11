#pragma once
// KICK GROOVE: where SF KICK's kicks land against the host's beat, for the plugins that cut or start
// on Live's grid: REPEAT's start, ENERGY's chop, DECK's starts and gate, GATE's steps, STRIP's
// pattern, DUCK's pump. A sequencer clocked by Live plays after Live's beat [Nov 7-8]: the OXI
// One's notes reached Live 30-45 ms late (more at faster tempos), MIDI Clock Sync Delay didn't move
// them, and Live can't follow the ONE's clock instead (with EXT on it turns delay compensation
// off). Every part comes from the ONE, so they are all as late as the kick: these plugins move
// their grid by where the kick lands.
//
// From SF KICK's reports: each kick's distance from the nearest beat, the last 8 within 16 s; the
// median of the largest group that agrees within 4 ms (4 or more, and most of them), as HeardBeat.
// Held while no kick comes (a breakdown); 0 until the first agreement. It is the lateness on a
// track that plays the ONE's notes: a plugin with latency upstream of it (a group after Pocket
// instruments: 512 samples) hears the groove that much later still. SF BUILD measures its own
// input instead (HeardBeat): the LIVE BUS was another 40 ms behind its song position.
#include "Bus.h"
#include "HeardBeat.h"
#include <cmath>

namespace sf
{
class KickGroove
{
public:
    static constexpr double kWithinSec = 16.0;

    // Seconds the kick lands after the host's beat (+); recomputed when a kick is reported.
    double seconds (Bus& bus, double bpm)
    {
        const uint32_t w = bus.d().kickWrite.load();
        if (w == seen || bpm <= 0.0) return sec;
        seen = w;
        KickHit h[kKickRing];
        const int n = bus.recentKicks (h, kKickRing);
        double v[HeardBeat::kKeep];
        int m = 0;
        const double now = Bus::nowSec();
        for (int k = 0; k < n && m < HeardBeat::kKeep; ++k)
            if (now - h[k].wall < kWithinSec) v[m++] = (h[k].ppq - std::round (h[k].ppq)) * 60.0 / bpm;
        double med;
        if (HeardBeat::agreedMedian (v, m, med)) { sec = med; locked = true; }
        return sec;
    }
    double beats (Bus& bus, double bpm) { return seconds (bus, bpm) * bpm / 60.0; }
    bool isLocked() const { return locked; }

private:
    uint32_t seen = 0xffffffffu;
    double sec = 0.0;
    bool locked = false;
};
}
