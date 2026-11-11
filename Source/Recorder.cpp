#include "Recorder.h"

namespace sf0
{
using juce::String;

// The set at 24 bits (the limiter holds it under full scale). The lanes and returns in 32-bit
// float: a lane can run over full scale on its way to the mix, and a float file cannot clip.
static int bitsFor (int track) { return track == 0 ? 24 : 32; }

String Recorder::trackName (int track)
{
    if (track == 0) return "00 SET";
    if (track <= kLanes) return String (track).paddedLeft ('0', 2) + " " + lanes()[(size_t) (track - 1)].name;
    return track == kLanes + 1 ? String (kLanes + 1) + " DUB" : String (kLanes + 2) + " SPACE";
}

double Recorder::bytesAnHour() const
{
    double perFrame = 0.0;
    for (int t = 0; t < kTracks; ++t) perFrame += 2.0 * bitsFor (t) / 8.0;
    return perFrame * engine.sampleRate() * 3600.0;
}

Recorder::Recorder (Engine& e) : juce::Thread ("SF-000 recorder"), engine (e)
{
    root = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SF-000");
}

Recorder::~Recorder() { stopTimer(); stop(); }

static String hms (double seconds)
{
    const auto s = (juce::int64) std::floor (seconds);
    return String (s / 3600) + ":" + String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + String (s % 60).paddedLeft ('0', 2);
}

// How a folder is said on the screen: from the home folder down.
static String spoken (const juce::File& f)
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return (f.isAChildOf (home) ? f.getRelativePathFrom (home) : f.getFullPathName()).toUpperCase();
}

String Recorder::start()
{
    if (on.load()) return {};
    rate = engine.sampleRate();
    if (rate < 8000.0) return "NO AUDIO DEVICE IS RUNNING";
    began = juce::Time::getCurrentTime();
    const String stamp = began.formatted ("%Y-%m-%d %H.%M.%S");
    dir = root.getChildFile (stamp);
    for (int k = 2; dir.exists(); ++k) dir = root.getChildFile (stamp + " " + String (k));
    if (! dir.createDirectory()) return "CANNOT MAKE " + spoken (dir);
    // room for ten minutes at least
    const auto freeBytes = (double) dir.getBytesFreeOnVolume();
    if (freeBytes < bytesAnHour() / 6.0) { dir.deleteRecursively(); return "THE DISK IS NEARLY FULL"; }

    // the lanes and returns leave the engine before the master chain has held the set back for its
    // look-ahead: each begins with that much silence, and lines up with the set
    stemLead = engine.masterLatency();
    juce::WavAudioFormat wav;
    for (int t = 0; t < kTracks; ++t)
    {
        files[(size_t) t] = dir.getChildFile (trackName (t) + ".wav");
        auto stream = files[(size_t) t].createOutputStream();
        if (stream != nullptr)
        {
            writers[(size_t) t].reset (wav.createWriterFor (stream.get(), rate, 2, bitsFor (t), {}, 0));
            if (writers[(size_t) t] != nullptr) stream.release();          // the writer owns it now
        }
        if (writers[(size_t) t] == nullptr)
        {
            for (auto& w : writers) w = nullptr;
            dir.deleteRecursively();
            return "CANNOT WRITE IN " + spoken (root);
        }
        if (t > 0 && stemLead > 0)
        {
            juce::AudioBuffer<float> z (2, stemLead);
            z.clear();
            writers[(size_t) t]->writeFromAudioSampleBuffer (z, 0, stemLead);
        }
    }
    if (stateAtStart) if (const auto f = stateAtStart(); f.existsAsFile()) f.copyFileTo (dir.getChildFile ("session-at-start.json"));
    tempoAtStart = engine.tempo();

    ringSize = juce::nextPowerOfTwo ((int) (rate * 4.0));                   // four seconds for the disk to catch up in
    for (auto& r : ring) r.assign ((size_t) ringSize, 0.0f);
    fifo.setTotalSize (ringSize);
    fifo.reset();
    peak.fill (0.0f);
    frames = 0; lostFrames = 0; lostTimes = 0; sinceFlush = 0; failed = false; skipping = false;
    startThread (juce::Thread::Priority::normal);
    on = true;
    engine.setRecSink (this);                                               // from the next buffer on
    startTimer (1000);
    said = "RECORDING TO " + spoken (dir) + "  (" + String (freeBytes / 1.0e9, 0) + " GB FREE, " + String (freeBytes / bytesAnHour(), 0) + " HOURS)";
    return {};
}

void Recorder::stop() { finish ({}); }

void Recorder::finish (const String& why)
{
    if (! on.load()) return;
    stopTimer();
    engine.setRecSink (nullptr);                // returns once the audio thread has let go
    on = false;
    stopThread (10000);                         // it writes what is left in the ring and goes
    drain();
    for (auto& w : writers) w = nullptr;        // lengths written, files closed

    // a lane that made no sound all take is not kept
    juce::StringArray kept, silent;
    for (int t = 0; t < kTracks; ++t)
    {
        if (t > 0 && peak[(size_t) t] < 1.0e-5f) { files[(size_t) t].deleteFile(); silent.add (trackName (t)); }
        else kept.add (trackName (t));
    }
    const double len = seconds(), lost = lostSeconds();
    String n;
    n << "SF-000 TAKE\n"
      << "STARTED  " << began.formatted ("%Y-%m-%d %H:%M:%S") << "\n"
      << "LENGTH   " << hms (len) << "  (" << String (frames.load()) << " samples at " << String (rate, 0) << " Hz)\n"
      << "TEMPO AT THE START  " << String (tempoAtStart, 1) << "\n"
      << (why.isNotEmpty() ? "ENDED BY ITSELF: " + why + "\n" : String())
      << "LOST     " << (lost > 0.0 ? String (lost, 2) + " s in " + String (lostTimes.load()) + " places: the disk fell behind. Every file has the same gaps, so they are still in step."
                                    : String ("nothing")) << "\n\n"
      << "00 SET.wav is the set: the output, after the limiter. 24-bit.\n"
      << "The numbered lanes and the two returns (DUB, SPACE) are each as it went into the mix: after its\n"
      << "inserts, its EQ, MUTE and SOLO, before the master chain. 32-bit float, so none can clip. Added\n"
      << "together they are what the master chain was given.\n"
      << "Each of them begins with " << stemLead << " samples of silence, the master chain's look-ahead, so it lines up with the set.\n\n"
      << "KEPT: " << kept.joinIntoString (", ") << "\n"
      << (silent.isEmpty() ? String() : "NOT KEPT, SILENT THROUGHOUT: " + silent.joinIntoString (", ") + "\n")
      << (dir.getChildFile ("session-at-start.json").existsAsFile() ? "session-at-start.json is the rig as it stood when the take began.\n" : "");
    dir.getChildFile ("take.txt").replaceWithText (n);
    said = (why.isNotEmpty() ? why + ". " : String()) + "KEPT " + hms (len) + " IN " + spoken (dir) + (lost > 0.0 ? "  (" + String (lost, 1) + " S LOST)" : String());
}

// ------------------------------------------------------------------------------ the audio thread
void Recorder::begin (int n) noexcept
{
    fifo.prepareToWrite (n, w1, n1, w2, n2);
    skipping = n1 + n2 < n;                     // no room: the disk is behind. This buffer is left out of every file.
}

void Recorder::track (int index, const float* l, const float* r, int n) noexcept
{
    if (skipping || index < 0 || index >= kTracks) return;
    const float* src[2] = { l, r };
    float pk = peak[(size_t) index];
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& line = ring[(size_t) (index * 2 + ch)];
        juce::FloatVectorOperations::copy (line.data() + w1, src[ch], n1);
        if (n2 > 0) juce::FloatVectorOperations::copy (line.data() + w2, src[ch] + n1, n2);
        const auto range = juce::FloatVectorOperations::findMinAndMax (src[ch], n);
        pk = std::max (pk, std::max (-range.getStart(), range.getEnd()));
    }
    peak[(size_t) index] = pk;
}

void Recorder::end (int n) noexcept
{
    if (skipping) { lostFrames += n; lostTimes += 1; return; }
    fifo.finishedWrite (n1 + n2);
    frames += n;
}

// ----------------------------------------------------------------------------- the writer's thread
bool Recorder::drain()
{
    bool ok = true;
    for (;;)
    {
        int s1, c1, s2, c2;
        fifo.prepareToRead (fifo.getNumReady(), s1, c1, s2, c2);
        if (c1 + c2 <= 0) break;
        for (int t = 0; t < kTracks && ok; ++t)
        {
            auto* w = writers[(size_t) t].get();
            if (w == nullptr) continue;
            const float* a[2] = { ring[(size_t) (t * 2)].data() + s1, ring[(size_t) (t * 2 + 1)].data() + s1 };
            ok = w->writeFromFloatArrays (a, 2, c1);
            if (ok && c2 > 0)
            {
                const float* b[2] = { ring[(size_t) (t * 2)].data() + s2, ring[(size_t) (t * 2 + 1)].data() + s2 };
                ok = w->writeFromFloatArrays (b, 2, c2);
            }
        }
        fifo.finishedRead (c1 + c2);
        if (! ok) break;
        // Every ten seconds the lengths are written into the files' heads, so a take cut short by a
        // crash or a pulled plug still opens, up to there.
        sinceFlush += c1 + c2;
        if (sinceFlush > (juce::int64) (rate * 10.0)) { sinceFlush = 0; for (auto& w : writers) if (w != nullptr) ok = w->flush() && ok; }
    }
    return ok;
}

void Recorder::run()
{
    while (! threadShouldExit())
    {
        if (! failed.load() && ! drain()) failed = true;
        wait (20);
    }
}

// --------------------------------------------------------------------------- the message thread
void Recorder::timerCallback()
{
    if (! on.load()) return;
    if (failed.load()) finish ("THE DISK WOULD NOT TAKE IT");
    else if ((double) dir.getBytesFreeOnVolume() < bytesAnHour() / 60.0) finish ("THE DISK IS FULL");       // a minute left
    else if (std::abs (engine.sampleRate() - rate) > 0.5) finish ("THE SAMPLE RATE CHANGED");
}
} // namespace sf0
