#pragma once
// The cue's rig: a second SF-000 process that is the rig as a recall would leave it, so that what
// plays in the headphones is what the PA would get (DECISIONS 595 on). This is the block of
// memory the two processes share. The instrument's audio thread writes where its clock is and
// reads the second rig's sound; the second rig reads the clock and writes its sound. Atomics
// only: neither ever waits for the other, and the instrument's audio never depends on the second
// rig being there.
#include <atomic>
#include <cstdint>

namespace sf0
{
struct CueShared
{
    static constexpr uint32_t kMagic = 0x53464355u;         // "SFCU"
    static constexpr uint32_t kVersion = 1;
    static constexpr int kFrames = 1 << 15;                 // the ring: two thirds of a second at 48 kHz (a power of two)

    std::atomic<uint32_t> magic, version;

    // ---- the instrument's clock, written by its audio thread at the end of every buffer.
    // seq is odd while the four below it are being written: a reader takes them again.
    std::atomic<uint32_t> seq;
    std::atomic<int64_t> devPos;                            // the sample its next buffer starts at (every sample it has played, counted)
    std::atomic<double> ppq;                                // the song position there
    std::atomic<double> bpm;
    std::atomic<int32_t> running;
    std::atomic<int32_t> block;                             // how many samples its device asks for at a time
    std::atomic<int32_t> want;                              // 1: a cue is wanted, or one is still fading away: the second rig renders. 0: it rests

    // ---- the second rig's sound, written by its render thread.
    std::atomic<int32_t> serial;                            // which cue the frames from validFrom on are
    std::atomic<int64_t> validFrom;                         // (samples before this are an earlier cue's, or nothing)
    std::atomic<int64_t> written;                           // the frames up to here are in the ring
    std::atomic<int64_t> beat;                              // counts up while the second rig is alive (the instrument's SET UP reads it)
    std::atomic<float> load;                                // what its rendering takes of real time, smoothed

    float ring[kFrames * 2];                                // left, right, a frame at (its sample & (kFrames - 1))
};

static_assert (std::atomic<int64_t>::is_always_lock_free && std::atomic<double>::is_always_lock_free, "the cue's shared block needs lock-free 64-bit atomics");
} // namespace sf0
