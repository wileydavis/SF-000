#pragma once
// GLIDE RINGS, SF CONTROL's side: from the plugins' glide reports on the bus, the SysEx messages
// that tell the E16 scene's script (controllers/e16/glide_rings.lua) which knobs are travelling
// and where their sound is now.
//   F0 7D 53 46 01 {channel - 1, number, value / 128, value % 128} x up to 14  F7   (value 0-16383)
//   F0 7D 53 46 02 {channel - 1, number} x up to 28                              F7   (arrived)
// 7D is MIDI's non-commercial id; 53 46 is "SF". A knob is reported when it starts travelling,
// whenever its ring moves by 32 or more (of 16383), and at least every 400 ms: the script lets a
// ring go after 1.2 s without a report, so a stopped CONTROL leaves no green ring behind.
#include "Bus.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdlib>
#include <vector>

namespace sf
{
class E16Glide
{
public:
    static constexpr int kStep = 32, kKeepMs = 400, kPerMove = 14, kPerDone = 28;

    // Message thread (SF CONTROL's timer). nowMs: Bus::nowMs().
    std::vector<std::vector<uint8_t>> collect (Bus& bus, int64_t nowMs)
    {
        auto& d = bus.d();
        std::bitset<kAddr> on;
        for (int s = 0; s < kGlideSlots; ++s)
        {
            const auto& g = d.glides[s];
            if (g.owner.load() == 0 || nowMs - g.seenMs.load() > 500) continue;
            const int n = std::min (kGlideEntries, std::max (0, (int) g.count.load()));
            for (int k = 0; k < n; ++k)
            {
                const uint32_t e = g.entry[k].load();
                const int ch = (int) (e >> 24), num = (int) ((e >> 16) & 0xffu);
                if (ch < 1 || ch > 16 || num > 127) continue;
                const int a = (ch - 1) * 128 + num;
                on.set ((size_t) a);
                pos[(size_t) a] = (uint16_t) std::min (16383u, e & 0xffffu);
            }
        }
        std::vector<std::vector<uint8_t>> out;
        std::vector<uint8_t> msg;
        int count = 0;
        auto open = [&] (uint8_t kind) { msg = { 0xF0, 0x7D, 0x53, 0x46, kind }; count = 0; };
        auto close = [&] { if (count > 0) { msg.push_back (0xF7); out.push_back (msg); } };
        open (1);
        for (int a = 0; a < kAddr; ++a)
        {
            if (! on[(size_t) a]) continue;
            const int p = pos[(size_t) a];
            if (active[(size_t) a] && std::abs (p - (int) last[(size_t) a]) < kStep && nowMs - sentAt[(size_t) a] < kKeepMs) continue;
            if (count == kPerMove) { close(); open (1); }
            msg.insert (msg.end(), { (uint8_t) (a >> 7), (uint8_t) (a & 127), (uint8_t) (p >> 7), (uint8_t) (p & 127) });
            ++count;
            last[(size_t) a] = (uint16_t) p;
            sentAt[(size_t) a] = nowMs;
        }
        close();
        open (2);
        for (int a = 0; a < kAddr; ++a)
        {
            if (! active[(size_t) a] || on[(size_t) a]) continue;
            if (count == kPerDone) { close(); open (2); }
            msg.insert (msg.end(), { (uint8_t) (a >> 7), (uint8_t) (a & 127) });
            ++count;
        }
        close();
        active = on;
        return out;
    }

    // The port changed or closed: the next report of every knob counts as a start.
    void reset() { active.reset(); }

private:
    static constexpr int kAddr = 16 * 128;
    std::bitset<kAddr> active;
    std::array<uint16_t, kAddr> pos {}, last {};
    std::array<int64_t, kAddr> sentAt {};
};
} // namespace sf
