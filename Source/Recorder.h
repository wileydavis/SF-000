#pragma once
// SF-000 recording: the set and its parts, written to disk off the audio thread.
//   A take is a folder: the set (the output, after the limiter), each lane as it went into the
//   mix, and the two returns as they did. The audio thread copies each buffer into a ring; a
//   thread of its own writes the ring to the files. If the disk falls behind, whole buffers are
//   left out of every file alike, so the files stay in step with one another, and the take says
//   how much was lost.
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"

namespace sf0
{
class Recorder : private Engine::RecSink, private juce::Thread, private juce::Timer
{
public:
    explicit Recorder (Engine&);
    ~Recorder() override;                       // a take under way is finished and kept

    // Message thread. start: empty if it is recording, else what went wrong. stop: the take is
    // finished (lanes that were silent throughout are not kept) and its notes are written.
    juce::String start();
    void stop();

    bool recording() const                      { return on.load(); }
    double seconds() const                      { return (double) frames.load() / rate; }
    double lostSeconds() const                  { return (double) lostFrames.load() / rate; }   // left out: the disk fell behind
    juce::File folder() const                   { return dir; }                                  // the take under way, or the last
    juce::String note() const                   { return said; }                                 // the last thing worth telling (where a take went, why one ended)
    double bytesAnHour() const;                 // at the engine's sample rate

    juce::File root;                            // where takes go: Music/SF-000 (a test points it elsewhere)
    std::function<juce::File()> stateAtStart;   // the rig as it stands, written to a file: copied into the take as it begins

    static juce::String trackName (int track);  // "00 SET", "01 KICK" ... "12 DUB", "13 SPACE"

private:
    void begin (int n) noexcept override;
    void track (int index, const float* l, const float* r, int n) noexcept override;
    void end (int n) noexcept override;
    void run() override;                        // the writer's thread
    void timerCallback() override;              // message thread: a full disk or a failed write ends the take
    bool drain();                               // ring to files; false if a write failed
    void finish (const juce::String& why);

    static constexpr int kTracks = Engine::kRecTracks;
    Engine& engine;
    double rate = 48000.0;
    int stemLead = 0;                           // samples of silence each lane and return begins with, to line up with the set

    // the ring: one line for each channel of each track, one position for all of them
    int ringSize = 0;
    juce::AbstractFifo fifo { 16 };
    std::array<std::vector<float>, kTracks * 2> ring;
    int w1 = 0, n1 = 0, w2 = 0, n2 = 0;         // audio thread: where this buffer goes
    bool skipping = false;
    std::array<float, kTracks> peak {};         // audio thread writes; read once the take is over

    std::array<std::unique_ptr<juce::AudioFormatWriter>, kTracks> writers;
    std::array<juce::File, kTracks> files;
    std::atomic<bool> on { false }, failed { false };
    std::atomic<juce::int64> frames { 0 }, lostFrames { 0 };
    std::atomic<int> lostTimes { 0 };
    juce::int64 sinceFlush = 0;
    juce::File dir;
    juce::Time began;
    double tempoAtStart = 0.0;
    juce::String said;
};
} // namespace sf0
