#include "Bus.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <random>

#if defined(__APPLE__) || defined(__linux__)
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
 #define SF_POSIX_SHM 1
#endif

namespace sf
{
const char* const kRoleNames[kNumRoles] = { "KICK", "BASS", "VOICE", "LEAD", "PERC", "RUMBLE", "PAD", "DECK", "OTHER" };
const float kBandEdges[kBands + 1] = { 20.0f, 60.0f, 100.0f, 150.0f, 220.0f, 320.0f, 480.0f, 750.0f, 1500.0f, 4000.0f, 20000.0f };
const float kBandCentres[kBands] = { 40.0f, 78.0f, 122.0f, 182.0f, 265.0f, 392.0f, 600.0f, 1060.0f, 2450.0f, 8900.0f };

static constexpr uint32_t kMagic = 0x5346424Bu;   // "SFBK"
static constexpr uint32_t kVersion = 12;
// Short name: macOS limits POSIX shm names to 31 chars. Bump with kVersion.
static constexpr const char* kShmName = "/sf_000_bus_v1";

// (SF-000) The clock: the wall's, plus what renders have put it ahead or behind (clockSkew); held
// while one runs (clockHeld: it moves as the render says). See Bus.h, renderClockBegin.
static std::atomic<bool> clockHolding { false };
static std::atomic<double> clockHeld { 0.0 }, clockSkew { 0.0 };
static double wallSec()
{
    using namespace std::chrono;
    return duration<double> (steady_clock::now().time_since_epoch()).count();
}
static double clockSec() { return clockHolding.load (std::memory_order_acquire) ? clockHeld.load (std::memory_order_acquire) : wallSec() + clockSkew.load (std::memory_order_acquire); }

double Bus::nowSec()
{
    if (testClock != nullptr) return testClock();
    return clockSec();
}

int64_t Bus::nowMs() { return (int64_t) std::llround (clockSec() * 1000.0); }

void Bus::renderClockBegin()
{
    clockHeld.store (clockSec(), std::memory_order_release);
    clockHolding.store (true, std::memory_order_release);
    get().forgetKicks();
}
void Bus::renderClockAdvance (double seconds)
{
    if (clockHolding.load (std::memory_order_acquire) && seconds > 0.0) clockHeld.store (clockHeld.load (std::memory_order_acquire) + seconds, std::memory_order_release);
}
void Bus::renderClockEnd()
{
    if (! clockHolding.load (std::memory_order_acquire)) return;
    clockSkew.store (clockHeld.load (std::memory_order_acquire) - wallSec(), std::memory_order_release);
    clockHolding.store (false, std::memory_order_release);
    get().forgetKicks();
}
// Every report made so far is from long ago, and no kick has been seen: for a rig that starts
// where another left off (its own song position, its own time).
void Bus::forgetKicks()
{
    for (int i = 0; i < kKickRing; ++i)
    {
        auto& s = data->kicks[i];
        const uint32_t q = s.seq.load();
        s.seq.store (q | 1u);
        s.wall = -1.0e12;
        s.seq.store ((q | 1u) + 1u);
    }
    data->kickSeenMs = (int64_t) -1.0e15;
}

Bus& Bus::get()
{
    static Bus bus;
    return bus;
}

Bus::Bus()
{
    static_assert (std::atomic<float>::is_always_lock_free, "lock-free float atomics required");
    static_assert (std::atomic<double>::is_always_lock_free, "lock-free double atomics required");
    static_assert (std::atomic<int64_t>::is_always_lock_free, "lock-free 64-bit atomics required");
#if SF_POSIX_SHM
    const int fd = shm_open (kShmName, O_RDWR | O_CREAT, 0666);
    if (fd >= 0)
    {
        struct stat st {};
        if (fstat (fd, &st) == 0 && st.st_size == 0)
            (void) ftruncate (fd, (off_t) sizeof (BusData));   // new pages are zero: a valid empty bus
        // A segment too small for this build is never mapped: use a local bus. (Not an exact
        // match: macOS reports the size rounded up to whole pages. The layout check is below.)
        if (fstat (fd, &st) == 0 && st.st_size >= (off_t) sizeof (BusData))
        {
            void* m = mmap (nullptr, sizeof (BusData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (m != MAP_FAILED)
            {
                auto* candidate = static_cast<BusData*> (m);
                // Same magic, version and layout size (a build with another layout under the same
                // name would read every field at the wrong place).
                const uint32_t magic = candidate->magic.load(), ver = candidate->version.load(), lay = candidate->layoutSize.load();
                if (magic == 0 || (magic == kMagic && (ver == kVersion || ver == 0) && (lay == (uint32_t) sizeof (BusData) || lay == 0)))
                {
                    data = candidate;
                    isShared = true;
                }
                else munmap (m, sizeof (BusData));
            }
        }
        close (fd);
    }
#endif
    if (data == nullptr)
    {
        // Fallback: process-local bus (plugins in one process still talk).
        static BusData local {};
        std::memset ((void*) &local, 0, sizeof (local));
        data = &local;
    }
    uint32_t expected = 0;
    if (data->magic.compare_exchange_strong (expected, kMagic))
    {
        data->version.store (kVersion);
        data->layoutSize.store ((uint32_t) sizeof (BusData));
        data->song = -1;
        data->keyRoot = -1;
        data->songAtPpq = -1.0;
        data->songKey = -2;
        data->panicHeld = 0;
        data->dropPpq = -1.0;
        data->cutAtPpq = -1.0;
        data->cutBackPpq = -1.0;
        data->storeSong = -1;
        data->songFromPpq = -1.0;
        data->slowAll = 0;
        data->slowAllPrev = 0;
        data->slowAllFromPpq = -1.0;
        for (auto& m : data->macro) m = 0.0f;
    }
}

void Bus::publishKick (const KickHit& h)
{
    const uint32_t i = data->kickWrite.fetch_add (1) % (uint32_t) kKickRing;
    auto& s = data->kicks[i];
    const uint32_t q = s.seq.load();
    s.seq.store (q | 1u);                    // odd: being written
    s.ppq = h.ppq; s.vel = h.vel; s.f0 = h.f0; s.f1 = h.f1; s.tau = h.tau;
    s.decay = h.decay; s.hp = h.hp; s.lp = h.lp; s.wall = nowSec();
    s.seq.store ((q | 1u) + 1u);             // even: stable
    data->kickSeenMs = nowMs();
}

int Bus::recentKicks (KickHit* out, int max) const
{
    const uint32_t w = data->kickWrite.load();
    int n = 0;
    for (int k = 1; k <= kKickRing && n < max; ++k)
    {
        if ((uint32_t) k > w) break;
        const auto& s = data->kicks[(w - (uint32_t) k) % (uint32_t) kKickRing];
        const uint32_t q0 = s.seq.load();
        if (q0 & 1u) continue;
        KickHit h { s.ppq.load(), s.vel.load(), s.f0.load(), s.f1.load(), s.tau.load(), s.decay.load(), s.hp.load(), s.lp.load(), s.wall.load() };
        if (s.seq.load() != q0) continue;
        out[n++] = h;
    }
    return n;
}

bool Bus::kickAlive (int64_t withinMs) const { return nowMs() - data->kickSeenMs.load() < withinMs; }

void Bus::packName (std::atomic<uint64_t>* dst, const char* s)
{
    char buf[24] {};
    std::strncpy (buf, s, 23);
    for (int i = 0; i < 3; ++i)
    {
        uint64_t v = 0;
        std::memcpy (&v, buf + i * 8, 8);
        dst[i] = v;
    }
}

void Bus::unpackName (const std::atomic<uint64_t>* src, char* out24)
{
    for (int i = 0; i < 3; ++i)
    {
        const uint64_t v = src[i].load();
        std::memcpy (out24 + i * 8, &v, 8);
    }
    out24[23] = 0;
}

bool Bus::sourceAlive (int slot, int64_t withinMs) const
{
    if (slot < 0 || slot >= kSources) return false;
    const auto& s = data->src[slot];
    return s.owner.load() != 0 && nowMs() - s.seenMs.load() < withinMs;
}

int Bus::claimSource (uint64_t owner, int role, const char* name)
{
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < kSources; ++i)
        {
            auto& s = data->src[i];
            uint64_t cur = s.owner.load();
            const bool stale = cur != 0 && nowMs() - s.seenMs.load() > 5000;
            if (cur == owner) return i;
            if ((pass == 0 && cur == 0) || (pass == 1 && stale))
            {
                if (s.owner.compare_exchange_strong (cur, owner))
                {
                    s.seenMs = nowMs();
                    s.role = role;
                    s.enabled = 1;
                    s.uid = 0;
                    packName (s.name, name);
                    for (int b = 0; b < kBands; ++b) { s.energy[b] = -120.0f; s.cut[b] = 0.0f; }
                    return i;
                }
            }
        }
    return -1;
}

void Bus::touchSource (int slot, uint64_t owner, int role, bool enabled)
{
    if (slot < 0 || slot >= kSources) return;
    auto& s = data->src[slot];
    if (s.owner.load() != owner) return;
    s.seenMs = nowMs();
    s.role = role;
    s.enabled = enabled ? 1 : 0;
}

void Bus::releaseSource (int slot, uint64_t owner)
{
    if (slot < 0 || slot >= kSources) return;
    auto& s = data->src[slot];
    uint64_t cur = owner;
    s.owner.compare_exchange_strong (cur, 0);
}

int Bus::claimGlide (uint64_t owner)
{
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < kGlideSlots; ++i)
        {
            auto& g = data->glides[i];
            uint64_t cur = g.owner.load();
            if (cur == owner) return i;
            const bool stale = cur != 0 && nowMs() - g.seenMs.load() > 1500;
            if ((pass == 0 && cur == 0) || (pass == 1 && stale))
                if (g.owner.compare_exchange_strong (cur, owner))
                {
                    g.count = 0;
                    g.seenMs = nowMs();
                    return i;
                }
        }
    return -1;
}

void Bus::releaseGlide (int slot, uint64_t owner)
{
    if (slot < 0 || slot >= kGlideSlots) return;
    auto& g = data->glides[slot];
    if (g.owner.load() != owner) return;
    g.count = 0;
    uint64_t cur = owner;
    g.owner.compare_exchange_strong (cur, 0);
}

bool Bus::glideAlive (int slot, int64_t withinMs) const
{
    if (slot < 0 || slot >= kGlideSlots) return false;
    const auto& g = data->glides[slot];
    return g.owner.load() != 0 && nowMs() - g.seenMs.load() < withinMs;
}
} // namespace sf
