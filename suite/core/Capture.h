#pragma once
// In-set place capture. SF TERROIR (on a return) can't send a sweep to a speaker of its own or hear
// microphones: its sound goes where its track goes, and in comes what the sends bring. So the room
// mics' own track holds SF CAPTURE: asked over the SF BUS (v10, BusData::cap*), it plays the sweep
// file out of its track's output (the headphone out, a speaker on it), records its input (the mics)
// on the same clock, writes the take, and TERROIR analyses it into the slot it asked for.
//
// Every take is one request, numbered (capSeq), from the ask to the import. Its state lives in one
// word with its number (capStatus = pack (request, state, error)) and changes only by advance(),
// which checks both: a recorder that lost its take (removed, its audio stopped, replaced by a newer
// request) can never change another request's state. TERROIR's STOP names the request (capCancel),
// so it holds at whatever stage the take has reached. The recorder that claimed a take keeps a lease
// while it runs (capOwnerSeenMs, from its audio): a take whose recorder stops answering for
// kLeaseMs is over (TERROIR ends it: Lost), and a new capture can be asked for.
#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace sf::capture
{
enum State : int32_t { Idle = 0, Preparing = 1, Playing = 2, Writing = 3, Done = 4, Failed = 5 };
enum Error : int32_t { None = 0, Cancelled = 1, TransportStarted = 2, CantWrite = 3, NoMemory = 4,
                       Lost = 5,        // its recorder stopped answering (its audio stood still kLeaseMs)
                       NoAnswer = 6,    // no SF CAPTURE took the request within 3 s
                       Removed = 7 };   // the SF CAPTURE that had it was removed
constexpr double kTailSec = 3.0;            // recorded after the sweep file ends: the last sweep's room, the round trip
constexpr int64_t kLeaseMs = 2000;          // a take whose recorder's audio hasn't run for this long is over
inline std::string testFolder;              // tests only: takes go here, not to Music/SF Live Suite/TERROIR/Captures
// Tests only. testHold: SF CAPTURE's worker waits at these points while the bit is set (a fixed
// order of events in the lifecycle tests): before its claim, before handing a take over, before
// writing one, and with the file written but the take not yet Done. testShortTake: a 0.5 s 1 kHz
// tone instead of the sweep file (lifecycle tests that need a take, not a place).
enum : int { kHoldClaim = 1, kHoldPublish = 2, kHoldWrite = 4, kHoldDone = 8 };
inline std::atomic<int> testHold { 0 };
inline std::atomic<bool> testShortTake { false };

inline bool busy (int32_t state) { return state == Preparing || state == Playing || state == Writing; }

struct Status { uint32_t seq = 0; int32_t state = Idle, error = None; };
inline uint64_t pack (uint32_t seq, int32_t state, int32_t error)
{
    return ((uint64_t) seq << 32) | ((uint64_t) (uint32_t) (state & 0xff) << 8) | (uint64_t) (uint32_t) (error & 0xff);
}
inline Status unpack (uint64_t v) { return { (uint32_t) (v >> 32), (int32_t) ((v >> 8) & 0xff), (int32_t) (v & 0xff) }; }
inline Status status (const std::atomic<uint64_t>& s) { return unpack (s.load (std::memory_order_acquire)); }
constexpr uint32_t bit (int32_t st) { return 1u << (uint32_t) st; }
constexpr uint32_t kBusyMask = bit (Preparing) | bit (Playing) | bit (Writing);

// Request seq to (state, error), only while the word holds that request in one of the states in
// fromMask (bits): false if it holds another request, or that one in another state.
inline bool advance (std::atomic<uint64_t>& s, uint32_t seq, uint32_t fromMask, int32_t state, int32_t error)
{
    uint64_t cur = s.load (std::memory_order_acquire);
    for (;;)
    {
        const auto c = unpack (cur);
        if (c.seq != seq || ((fromMask >> (uint32_t) c.state) & 1u) == 0) return false;
        if (s.compare_exchange_weak (cur, pack (seq, state, error), std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    }
}

// Where a take goes: one file per request, named by when it was asked for (the slot takes the name).
inline juce::File folder()
{
    if (! testFolder.empty()) return juce::File (testFolder);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SF Live Suite")
               .getChildFile ("TERROIR").getChildFile ("Captures");
}
inline juce::File fileFor (int64_t stampMs)
{
    return folder().getChildFile ("ROOM " + juce::Time (stampMs).formatted ("%Y-%m-%d %H.%M.%S") + ".wav");
}
} // namespace sf::capture
