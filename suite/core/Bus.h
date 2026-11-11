#pragma once
// SF BUS: one block of shared memory that every SF plugin maps (across plugin binaries and
// processes). Lock-free atomics only. Carries macros, song recall, kick reports (Pocket) and
// per-band source energies / cuts (ALLOC).
#include <atomic>
#include <cstdint>

namespace sf
{
constexpr int kBands = 10;
constexpr int kSources = 32;
constexpr int kKickRing = 32;
constexpr int kMacros = 6;
constexpr int kSongs = 16;
constexpr int kConflictSlots = 3;
constexpr int kGlideSlots = 48;          // one per SF plugin instance that has a knob on the E16
constexpr int kGlideEntries = 8;         // knobs of one plugin travelling at once (its performance row)
constexpr int kSlowAllTop = 8;           // SLOW ALL's last index, 128 BAR (slowAll below)

// Default ALLOC priority, highest first.
enum Role { ROLE_KICK = 0, ROLE_BASS, ROLE_VOICE, ROLE_LEAD, ROLE_PERC, ROLE_RUMBLE, ROLE_PAD, ROLE_DECK, ROLE_OTHER, kNumRoles };
// Drums and whole tracks don't duck under SF KICK after SF BUILD's drop (Pocket::dropDuck).
inline bool dropDucks (int role) { return role != ROLE_KICK && role != ROLE_PERC && role != ROLE_DECK; }
extern const char* const kRoleNames[kNumRoles];
extern const float kBandCentres[kBands];     // Hz
extern const float kBandEdges[kBands + 1];   // Hz

// wall = steady-clock seconds when reported: lets readers reject reports left over from before
// a transport jump (loop brace, restart, relocate) that happen to share a song position.
struct KickHit { double ppq; float vel, f0, f1, tau, decay, hp, lp; double wall = 0.0; };

struct KickSlot
{
    std::atomic<uint32_t> seq;
    std::atomic<double> ppq, wall;
    std::atomic<float> vel, f0, f1, tau, decay, hp, lp;
};

struct SourceSlot
{
    std::atomic<uint64_t> owner;
    std::atomic<int64_t> seenMs;
    std::atomic<int32_t> role;
    std::atomic<int32_t> enabled;
    std::atomic<uint64_t> name[3];            // up to 24 chars, packed
    std::atomic<uint64_t> uid;                // the plugin instance's id, saved with the set (0 = not yet)
    std::atomic<float> energy[kBands];        // dBFS, smoothed
    std::atomic<float> cut[kBands];           // dB of cut ALLOC asks for (>= 0)
};

// A plugin's knobs that are travelling (SLOW), for SF CONTROL to show on the E16 (v7). Each entry
// is one knob: E16 channel (1-16) << 24 | number << 16 | ring position (0-16383, Live's mapping of
// where the sound is now). Written by the plugin every block, entries before count.
struct GlideSlot
{
    std::atomic<uint64_t> owner;
    std::atomic<int64_t> seenMs;
    std::atomic<int32_t> count;
    std::atomic<uint32_t> entry[kGlideEntries];
};

struct ConflictSlot
{
    std::atomic<uint32_t> sources;            // bit mask of source slots
    std::atomic<int32_t> bandLo, bandHi;
    std::atomic<int32_t> active;
};

struct BusData
{
    std::atomic<uint32_t> magic;
    std::atomic<uint32_t> version;
    std::atomic<uint32_t> layoutSize;         // sizeof (BusData) of the build that made it
    std::atomic<float> macro[kMacros];
    std::atomic<int32_t> song;                // -1 = none chosen yet
    std::atomic<uint32_t> songSeq;
    std::atomic<double> songAtPpq;            // the bar line every plugin recalls on (-1 = at once / own next bar)
    std::atomic<int32_t> songKey;             // the KEY that song brings (-1 = OFF, -2 = no change), switched on that bar
    std::atomic<uint32_t> storeSeq;
    std::atomic<int64_t> controlSeenMs;
    std::atomic<uint32_t> panicSeq;           // SF CONTROL PANIC pressed: every SF plugin fades out and resets
    std::atomic<int32_t> panicHeld;           // ... and stays silent while it is held
    std::atomic<int32_t> keyRoot;             // SF CONTROL KEY: 0..11 = C..B, -1 = not set

    std::atomic<uint32_t> kickWrite;
    std::atomic<int64_t> kickSeenMs;
    std::atomic<float> pocketOffsetMs;      // SF CONTROL calibration: + = pockets later
    KickSlot kicks[kKickRing];

    SourceSlot src[kSources];
    std::atomic<int64_t> allocSeenMs;
    ConflictSlot conflicts[kConflictSlots];

    // SF BUILD's DROP (v5): for dropBeats after the drop's bar line every Pocket stage ducks
    // dropDb harder to each kick, so the kick lands on top. Published when DROP is pressed,
    // ahead of the bar; from/wall reject it after a jump back (as for kick reports).
    std::atomic<double> dropPpq;              // the bar line the drop lands on (-1 = none)
    std::atomic<double> dropFromPpq;          // song position when BUILD planned it
    std::atomic<double> dropWall;             // steady-clock seconds then
    std::atomic<float> dropDb;                // 0..12 dB
    std::atomic<float> dropBeats;             // length of the duck window

    // SF CONTROL's STORE TO (v5): the song its STORE ALL writes into, set before storeSeq moves
    // (-1: the bus song). A plugin's own STORE ignores it.
    std::atomic<int> storeSong;
    // Where the transport was when SF CONTROL asked for the song (v5): a plugin that sees the
    // request just after a jump back can tell whether it was asked for before or after the jump.
    std::atomic<double> songFromPpq;

    // SF CONTROL's SLOW ALL (v6): how long a new setting takes to arrive in every plugin whose own
    // SLOW is CONTROL. 0 = OFF, 1..8 = 1..128 BAR (SFProcessor::glideBars; 128 BAR v11, so a plugin
    // from before it never reads it as 64). Honoured only while CONTROL runs.
    std::atomic<int32_t> slowAll;

    // Travelling knobs, for SF CONTROL's E16 RINGS (v7).
    GlideSlot glides[kGlideSlots];

    // SLOW ALL changes (v8): one rule for every plugin, whatever order Live processes the tracks
    // in. A change counts from the next host block, the song position slowAllFromPpq: a plugin
    // whose block starts before it uses slowAllPrev (the plugins after SF CONTROL in that block
    // see the new value already, those before it can't). -1: at once (transport stopped). The
    // fence holds for that host block only: SF CONTROL sets slowAllFromPpq back to -1 in its next
    // one, and a plugin that has started a block at or after it takes it as spent (a loop back to
    // that song position is a new move's time, with the new length).
    // slowSeq: odd while SF CONTROL writes the three.
    std::atomic<uint32_t> slowSeq;
    std::atomic<int32_t> slowAllPrev;
    std::atomic<double> slowAllFromPpq;

    // Place capture (v10, Source/core/Capture.h). SF TERROIR asks: capStatus = (request, Idle),
    // capStamp and capSlot, then capSeq = the request. One SF CAPTURE claims it (capStatus from
    // (request, Idle) to (request, Preparing): capture::advance), plays the sweep out of its track and
    // records its input, writes the take and reports in capStatus, with its progress and the input's
    // peak. capStatus changes only for the request it holds, so a recorder that lost its take can't
    // change a newer one. capCancel = a request: TERROIR's STOP for it, at any stage. The claimant's
    // audio keeps capOwnerSeenMs fresh while the take is in progress (its lease, capture::kLeaseMs).
    // An SF CAPTURE is there at all while captureSeenMs is recent.
    std::atomic<uint32_t> capSeq, capCancel;
    std::atomic<int64_t> capStamp;                // when it was asked for (wall clock, ms): names the file
    std::atomic<int32_t> capSlot;
    std::atomic<uint64_t> capStatus;              // capture::pack (request, state, error)
    std::atomic<float> capProgress, capPeak;      // 0..1 through the take; the input's peak so far (linear)
    std::atomic<int64_t> captureSeenMs, capOwnerSeenMs;

    // SF BUILD's DROP, the cut (v12). DROP pressed: every SF instrument (not DECK or CONTROL) and
    // every STRIP and TAP (ROLE not DECK) goes silent from cutAtPpq; pressed again: back from
    // cutBackPpq, the drop's kick. Both are song positions of the music (a plugin's own song
    // position less its latency: where its notes are), so the returns (DUB, SPACE) take no new
    // input and ring out. BUILD plans them where every plugin can still reach them: the next
    // sixteenth, then the next beat. A seqlock: cutSeq is odd while BUILD writes. from / wall
    // reject a cut after a jump back (as for kick reports); buildSeenSec is BUILD's heartbeat
    // (Bus::nowSec): a cut is let go when BUILD has not run for a second.
    std::atomic<uint32_t> cutSeq;
    std::atomic<double> cutAtPpq;             // -1 = no cut
    std::atomic<double> cutBackPpq;           // -1 = not asked back yet
    std::atomic<double> cutFromPpq;           // song position (Live's) when BUILD planned it
    std::atomic<double> cutWall;              // Bus::nowSec then
    std::atomic<double> buildSeenSec;
};

// A cut as a plugin reads it: the seqlock's fields, read whole (or the last whole read kept).
struct DropCut
{
    double at = -1.0, back = -1.0, from = 0.0, wall = 0.0;
    void read (const BusData& d)
    {
        const uint32_t s1 = d.cutSeq.load (std::memory_order_acquire);
        if ((s1 & 1u) != 0u) return;
        const DropCut r { d.cutAtPpq.load(), d.cutBackPpq.load(), d.cutFromPpq.load(), d.cutWall.load() };
        if (d.cutSeq.load (std::memory_order_acquire) == s1) *this = r;
    }
};

class Bus
{
public:
    static Bus& get();
    static int64_t nowMs();
    static double nowSec();
    static inline double (*testClock)() = nullptr;    // offline tests: simulated time

    BusData& d() { return *data; }
    bool shared() const { return isShared; }

    void publishKick (const KickHit&);
    int recentKicks (KickHit* out, int max) const;     // newest first
    // A report is current if the wall time since it was made fits the musical time since it.
    static bool kickCurrent (const KickHit& h, double dtBeats, double secPerBeat, double lookSec)
    {
        const double elapsed = nowSec() - h.wall;
        return elapsed <= dtBeats * secPerBeat + lookSec + 0.35;
    }
    bool kickAlive (int64_t withinMs = 4000) const;

    int claimSource (uint64_t owner, int role, const char* name);   // -1 if full
    void touchSource (int slot, uint64_t owner, int role, bool enabled);
    void releaseSource (int slot, uint64_t owner);
    bool sourceAlive (int slot, int64_t withinMs = 1500) const;
    static void packName (std::atomic<uint64_t>* dst, const char* s);
    static void unpackName (const std::atomic<uint64_t>* src, char* out24);

    // Glide reports (SF CONTROL's E16 RINGS). claim: -1 if full. A slot unseen for 1.5 s is stale.
    int claimGlide (uint64_t owner);
    void releaseGlide (int slot, uint64_t owner);
    bool glideAlive (int slot, int64_t withinMs = 500) const;

    bool allocAlive() const   { return nowMs() - data->allocSeenMs.load() < 1500; }
    bool controlAlive() const { return nowMs() - data->controlSeenMs.load() < 1500; }
    bool captureAlive() const { return nowMs() - data->captureSeenMs.load() < 1500; }

private:
    Bus();
    BusData* data = nullptr;
    bool isShared = false;
};
} // namespace sf
