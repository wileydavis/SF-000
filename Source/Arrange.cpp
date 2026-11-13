// The arrangement: snapshots in a line that morph one into the next, played from the clock, and
// printed to files. (Session's own: kept in a file of its own for its size.)
#include "Session.h"
#include "Recorder.h"
#include "core/Loudness.h"
#include "core/Bus.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace sf0
{
using juce::String;
using juce::var;

// How long before a part's bar line it is recalled: inside the beat before it, so that everything
// in the recall sets off on that bar line (a recall's knobs set off on the next beat).
static constexpr double kArrLead = 0.5;
// CUT, played: over this many beats (a little over the fiftieth of a second a crossing takes at
// the least, at any tempo this rig has), so that a cut, too, lands on the bar line.
static constexpr double kArrCut = 0.08;

String Session::morphName (int morph)
{
    static const char* const names[] = { "CUT", "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS", "64 BARS", "128 BARS", "256 BARS" };
    return names[juce::jlimit (0, 11, morph)];
}

int Session::arrBarsBefore (int part) const
{
    int bars = 0;
    for (int i = 0; i < part && i < (int) arrangement.size(); ++i) bars += arrangement[(size_t) i].bars;
    return bars;
}

double Session::arrMorphBars (int part) const
{
    if (part < 0 || part >= (int) arrangement.size()) return 0.0;
    const auto& p = arrangement[(size_t) part];
    return std::min ((double) p.bars, glideBarsFor ((float) juce::jlimit (0, 11, p.morph) / 11.0f));
}

bool Session::arrAdd (juce::int64 snap, int before)
{
    if ((int) arrangement.size() >= kArrMost || findSnap (snap) == nullptr) return false;
    ArrPart p;
    p.snap = snap;
    // (a part comes in as the one before it is: the same length, the same way in; the first: eight bars, cut)
    const int like = before >= 0 && before < (int) arrangement.size() ? before : (int) arrangement.size() - 1;
    if (like >= 0) { p.bars = arrangement[(size_t) like].bars; p.morph = arrangement[(size_t) like].morph; }
    if (before >= 0 && before < (int) arrangement.size()) arrangement.insert (arrangement.begin() + before, p); else arrangement.push_back (p);
    if (arrOn && before >= 0 && before <= arrAt) ++arrAt;
    touch();
    if (onChange) onChange();
    return true;
}

void Session::arrRemove (int part)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    arrangement.erase (arrangement.begin() + part);
    if (arrOn) { if (part <= arrAt) --arrAt; if (arrangement.empty()) arrStop(); }
    touch();
    if (onChange) onChange();
}

void Session::arrMove (int part, int by)
{
    const int to = part + by;
    if (part < 0 || part >= (int) arrangement.size() || to < 0 || to >= (int) arrangement.size() || by == 0) return;
    std::swap (arrangement[(size_t) part], arrangement[(size_t) to]);
    touch();
    if (onChange) onChange();
}

void Session::arrSet (int part, int bars, int morph)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    auto& p = arrangement[(size_t) part];
    if (bars >= 0) p.bars = juce::jlimit (1, kArrMaxBars, bars);
    if (morph >= 0) p.morph = juce::jlimit (0, 11, morph);
    touch();
}

void Session::recallOver (juce::int64 id, double beats, bool notes)
{
    glideOver = std::max (0.0, beats);
    Scope all;
    all.notes = notes;
    recall (id, all);
    glideOver = -1.0;
}

void Session::arrRecall (int part, double beats)
{
    if (part < 0 || part >= (int) arrangement.size()) return;
    // (printed, a part brings its notes whatever NOTES says: there is nothing outside to play them)
    recallOver (arrangement[(size_t) part].snap, beats, printRig || recallNotes);
}

void Session::arrPlay (int from)
{
    if (arrangement.empty()) return;
    from = juce::jlimit (0, (int) arrangement.size() - 1, from);
    arrOn = true; arrTicks = 0;
    if (! engine.isRunning())
    {
        // from stopped: that part is there at once, and the clock sets off from the top
        arrRecall (from, 0.0);
        arrAt = from;
        arrZero = -(double) arrBarsBefore (from) * 4.0;
        arrStarts = engine.starts(); arrWait = true;       // (until the clock has set off, the place it says is the last run's)
        engine.setRunning (true);
    }
    else
    {
        // while it runs: that part comes in on the next bar line it can be ready for
        const double bar = std::ceil ((engine.songPos() + kArrLead + 0.25) / 4.0) * 4.0;
        arrZero = bar - (double) arrBarsBefore (from) * 4.0;
        arrAt = from - 1; arrWait = false;
    }
    if (useDisk) startTimer (15);
    if (onChange) onChange();
}

void Session::arrStop()
{
    if (! arrOn) return;
    arrOn = false; arrAt = -1;
    engine.setRunning (false);
    if (onChange) onChange();
}

double Session::arrPos() const
{
    if (! arrOn || arrWait) return arrOn ? (double) arrBarsBefore (juce::jmax (0, arrAt)) * 4.0 : 0.0;
    return engine.songPos() - arrZero;
}

void Session::arrTick()
{
    if (! arrOn) return;
    if (arrWait) { if (engine.starts() == arrStarts) return; arrWait = false; }
    if (! engine.isRunning()) { arrOn = false; arrAt = -1; if (onChange) onChange(); return; }      // stopped by hand: it is put down
    const double pos = engine.songPos() - arrZero;
    const int next = arrAt + 1;
    if (next < (int) arrangement.size())
    {
        if (pos >= (double) arrBarsBefore (next) * 4.0 - kArrLead)
        {
            arrRecall (next, std::max (kArrCut, arrMorphBars (next) * 4.0));
            arrAt = next;
            if (onChange) onChange();
        }
    }
    else if (! printRig && pos >= (double) arrBars() * 4.0 - 0.06)
    {
        // its end: the clock stops, a moment short of the bar line so that nothing of the bar after is begun
        // (a print stops itself, on the sample)
        arrOn = false; arrAt = -1;
        engine.setRunning (false);
        if (onChange) onChange();
    }
}

// --------------------------------------------------------------------------------- the file
var Session::arrToVar() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<var> parts;
    for (const auto& p : arrangement)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("snap", p.snap);
        po->setProperty ("bars", p.bars);
        po->setProperty ("morph", glideBarsFor ((float) juce::jlimit (0, 11, p.morph) / 11.0f));       // (in bars: a later build may have other lengths)
        for (auto& m : p.more) po->setProperty (m.name, m.value);
        parts.add (var (po));
    }
    o->setProperty ("parts", parts);
    for (auto& m : arrMore) o->setProperty (m.name, m.value);
    return var (o);
}

void Session::arrFromVar (const var& v)
{
    arrangement.clear(); arrMore.clear();
    auto* o = v.getDynamicObject();
    if (o == nullptr) return;
    for (auto& p : o->getProperties()) if (p.name != juce::Identifier ("parts")) arrMore.set (p.name, p.value);
    if (auto* parts = v.getProperty ("parts", var()).getArray())
        for (auto& pv : *parts)
        {
            ArrPart p;
            p.snap = (juce::int64) pv.getProperty ("snap", 0);
            if (findSnap (p.snap) == nullptr) continue;             // (its snapshot has gone: so has the part)
            p.bars = juce::jlimit (1, kArrMaxBars, (int) pv.getProperty ("bars", 8));
            const double mb = (double) pv.getProperty ("morph", 0.0);
            p.morph = 0;
            for (int i = 0; i < 12; ++i) if (glideBarsFor ((float) i / 11.0f) <= mb + 1.0e-9) p.morph = i;      // the nearest length this build has, not over it
            if (auto* po = pv.getDynamicObject())
                for (auto& q : po->getProperties())
                    if (q.name != juce::Identifier ("snap") && q.name != juce::Identifier ("bars") && q.name != juce::Identifier ("morph")) p.more.set (q.name, q.value);
            arrangement.push_back (p);
        }
}

// -------------------------------------------------------------------------------- the print
// A rig of its own is made for it, from the session exactly as it stands (what the file would hold:
// the library, the scene, the snapshot knobs and their MOTION, the arrangement), and the
// arrangement is played on that, its render called here instead of by an audio device and the
// sink a take would be given written straight to files. Every module in it starts from its
// beginning, so the same arrangement prints the same file; and the rig that is being played is
// not touched (it only waits: one rig sounds at a time).
// The first samples each file is given are left out: the time the rig holds its sound back by, so
// that bar 1 begins on the file's first sample.
struct Session::Print : Engine::RecSink
{
    static constexpr int kTracks = Engine::kRecTracks, kBlock = 128;
    std::unique_ptr<Engine> eng;
    std::unique_ptr<Session> ses;
    juce::File dir;
    std::array<std::unique_ptr<juce::AudioFormatWriter>, kTracks> writers;
    std::array<juce::File, kTracks> files;
    std::array<juce::int64, kTracks> drop {};          // samples still to leave out of each
    std::array<float, kTracks> peak {};
    juce::int64 written = 0;                            // samples of the set written
    bool failed = false;
    sf::LoudnessMeter loud;
    float blockPeak = 0.0f;                             // the set's, this block
    juce::AudioBuffer<float> buf { 2, kBlock };

    enum Phase { play, tail } phase = play;
    juce::int64 total = 0, played = 0, tailed = 0, quiet = 0;
    double rate = 48000.0, tempo = 132.0;
    juce::Time began;

    void begin (int) noexcept override { blockPeak = 0.0f; }
    void end (int) noexcept override {}
    void track (int t, const float* l, const float* r, int n) noexcept override
    {
        if (t == 0) for (int i = 0; i < n; ++i) blockPeak = std::max (blockPeak, std::max (std::abs (l[i]), std::abs (r[i])));
        if (t < 0 || t >= kTracks || writers[(size_t) t] == nullptr) return;
        int from = 0;
        if (drop[(size_t) t] > 0) { from = (int) std::min ((juce::int64) n, drop[(size_t) t]); drop[(size_t) t] -= from; }
        if (from >= n) return;
        const float* ch[2] = { l + from, r + from };
        if (! writers[(size_t) t]->writeFromFloatArrays (ch, 2, n - from)) failed = true;
        for (int i = from; i < n; ++i) peak[(size_t) t] = std::max (peak[(size_t) t], std::max (std::abs (l[i]), std::abs (r[i])));
        if (t == 0) { for (int i = from; i < n; ++i) loud.push (l[i], r[i]); written += n - from; }
    }
};

float Session::printDone() const
{
    if (print == nullptr) return 0.0f;
    if (print->phase == Print::tail) return 0.99f;
    return (float) juce::jlimit (0.0, 0.98, (double) print->played / (double) std::max ((juce::int64) 1, print->total));
}

static String hmsOf (double seconds)
{
    const auto s = (juce::int64) std::llround (seconds);
    return String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
}

String Session::printBegin (const juce::File& root)
{
    if (print != nullptr) return "IT IS PRINTING";
    if (arrangement.empty()) return "NOTHING IN THE ARRANGEMENT";
    if (engine.hasRecSink()) return "A TAKE IS BEING RECORDED: END IT FIRST";
    if (! engine.isPrepared()) return "THE AUDIO HAS NOT STARTED";
    const juce::File into = root.isDirectory() || root.createDirectory() ? root : juce::File();
    if (into == juce::File()) return "CANNOT MAKE " + root.getFullPathName().toUpperCase();
    const auto began = juce::Time::getCurrentTime();
    juce::File dir = into.getChildFile ("PRINT " + began.formatted ("%Y-%m-%d %H.%M.%S"));
    for (int n = 2; dir.exists(); ++n) dir = into.getChildFile ("PRINT " + began.formatted ("%Y-%m-%d %H.%M.%S") + " " + String (n));
    if (! dir.createDirectory()) return "CANNOT MAKE " + dir.getFullPathName().toUpperCase();

    if (arrOn) arrStop();
    if (audioPause) audioPause();                       // one rig sounds at a time: this one waits
    sf::Bus::renderClockBegin();                        // (and the bus goes by the print's own time, not the wall's: DECISIONS 370)
    auto p = std::make_shared<Print>();
    p->dir = dir; p->began = began;
    p->rate = engine.sampleRate(); p->tempo = engine.tempo();
    // its own rig, as this one stands
    p->eng = std::make_unique<Engine>();
    p->eng->prepare (p->rate, Print::kBlock);
    p->eng->setOffline (true);
    p->ses = std::make_unique<Session> (*p->eng, false);
    p->ses->printRig = true;
    p->ses->takeState (stateVar());
    p->eng->setRunning (false);
    p->ses->arrRecall (0, 0.0);                         // its first part, there at once
    print = p;
    // The files. The set is held back by the lanes' wait and the master chain's; the lanes and the
    // returns by the lanes' alone: each file leaves out its own, and all begin on bar 1.
    juce::WavAudioFormat wav;
    for (int t = 0; t < Print::kTracks; ++t)
    {
        p->files[(size_t) t] = dir.getChildFile (Recorder::trackName (t) + ".wav");
        auto stream = p->files[(size_t) t].createOutputStream();
        if (stream != nullptr)
        {
            p->writers[(size_t) t].reset (wav.createWriterFor (stream.get(), p->rate, 2, t == 0 ? 24 : 32, {}, 0));
            if (p->writers[(size_t) t] != nullptr) stream.release();
        }
        if (p->writers[(size_t) t] == nullptr) { printEnd (false, "CANNOT WRITE IN " + dir.getFullPathName().toUpperCase()); return printNote; }
        p->drop[(size_t) t] = p->eng->laneLatency() + (t == 0 ? p->eng->masterLatency() : 0);
    }
    p->loud.prepare (p->rate);
    p->total = (juce::int64) std::llround ((double) arrBars() * 4.0 * 60.0 / p->tempo * p->rate);
    p->eng->setRecSink (p.get());
    p->ses->arrOn = true; p->ses->arrAt = 0; p->ses->arrZero = 0.0; p->ses->arrWait = false;
    p->eng->setRunning (true);
    printNote = "PRINTING";
    printFolder = dir;
    if (onChange) onChange();
    return {};
}

bool Session::printStep (double msToSpend)
{
    if (print == nullptr) return false;
    auto& p = *print;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto render = [&p] (int n)
    {
        p.buf.setSize (2, n, false, false, true);
        p.eng->render (p.buf);
        sf::Bus::renderClockAdvance ((double) n / p.rate);
    };
    while (juce::Time::getMillisecondCounterHiRes() - t0 < msToSpend)
    {
        if (p.failed) { printEnd (false, "THE DISK STOPPED TAKING IT"); return false; }
        if (p.phase == Print::play)
        {
            // to the sample its last bar ends on, and not one further
            const int n = (int) std::min ((juce::int64) Print::kBlock, p.total - p.played);
            if (n > 0) { render (n); p.played += n; p.ses->arrTick(); }
            if (p.played >= p.total)
            {
                p.ses->arrOn = false; p.ses->arrAt = -1;
                p.eng->setRunning (false);
                p.phase = Print::tail; p.quiet = 0;
            }
        }
        else
        {
            // what rings on after the last bar: until it has been silent for half a second (a minute at the most)
            render (Print::kBlock);
            p.tailed += Print::kBlock;
            p.quiet = p.blockPeak < 3.2e-5f ? p.quiet + Print::kBlock : 0;       // (-90 dB)
            const juce::int64 held = p.eng->laneLatency() + p.eng->masterLatency();     // (what the rig was still holding back has to come out, at the least)
            if ((p.tailed > held && p.quiet >= (juce::int64) (0.5 * p.rate)) || p.tailed >= (juce::int64) (60.0 * p.rate)) { printEnd (true, {}); return false; }
        }
    }
    return true;
}

void Session::printCancel() { if (print != nullptr) printEnd (false, "PRINT STOPPED"); }

void Session::printEnd (bool keep, const String& why)
{
    if (print == nullptr) return;
    auto p = print;
    p->eng->setRecSink (nullptr);
    p->eng->setRunning (false);
    sf::Bus::renderClockEnd();
    if (keep && p->writers[0] != nullptr && p->eng->masterLatency() > 0)
    {
        // (the set began the master chain's wait later than its parts: that much silence at its end, and all the files are one length)
        juce::AudioBuffer<float> z (2, p->eng->masterLatency());
        z.clear();
        p->writers[0]->writeFromAudioSampleBuffer (z, 0, z.getNumSamples());
    }
    for (auto& w : p->writers) w = nullptr;             // lengths written, files closed
    if (! keep) { p->dir.deleteRecursively(); printNote = why; }
    else
    {
        juce::StringArray silent;
        for (int t = 1; t < Print::kTracks; ++t)
            if (p->peak[(size_t) t] < 1.0e-5f) { p->files[(size_t) t].deleteFile(); silent.add (Recorder::trackName (t)); }
        const double len = (double) p->written / p->rate, music = (double) p->total / p->rate;
        const float pk = 20.0f * std::log10 (std::max (1.0e-9f, p->peak[0]));
        String n;
        n << "SF-000 PRINT\n"
          << "MADE     " << p->began.formatted ("%Y-%m-%d %H:%M:%S") << "\n"
          << "LENGTH   " << hmsOf (len) << "  (" << String (p->written) << " samples at " << String (p->rate, 0) << " Hz: " << String (arrBars()) << " bars at " << String (p->tempo, 1)
          << ", " << hmsOf (music) << ", and " << String (len - music, 1) << " s ringing on after the last)\n"
          << "LOUDNESS " << String (p->loud.integrated, 1) << " LUFS integrated, the highest sample at " << String (pk, 1) << " dBFS\n\n"
          << "THE ARRANGEMENT\n";
        for (int i = 0; i < (int) arrangement.size(); ++i)
        {
            const auto& part = arrangement[(size_t) i];
            const auto* sn = findSnap (part.snap);
            const int bar = arrBarsBefore (i);
            n << String (i + 1).paddedLeft (' ', 3) << "  BAR " << String (bar + 1).paddedLeft (' ', 4) << "  " << hmsOf ((double) bar * 4.0 * 60.0 / p->tempo).paddedLeft (' ', 6)
              << "  " << String (part.bars).paddedLeft (' ', 3) << (part.bars == 1 ? " BAR   " : " BARS  ") << (sn != nullptr ? sn->name : String ("(GONE)"))
              << (i == 0 ? String() : arrMorphBars (i) <= 0.0 ? String ("   CUT") : "   MORPH " + (part.morph > 0 && arrMorphBars (i) >= (double) part.bars && glideBarsFor ((float) part.morph / 11.0f) > (double) part.bars ? String ("ALL OF IT") : morphName (part.morph))) << "\n";
        }
        n << "\n00 SET.wav is the track: the output, after the limiter, 24-bit. Bar 1 begins on its first sample.\n"
          << "The numbered lanes, the two returns and the drum bus are the parts it was made of, as in a take\n"
          << "(32-bit float, each as it left its strip, before the master chain), in step with it.\n"
          << "It was rendered, not recorded: on a rig made for the purpose from the session as it stood, every\n"
          << "module from its beginning, so the same arrangement prints the same file. Nothing was played\n"
          << "into it from outside, and each part brought its own notes. The snapshot knobs stood where they\n"
          << "were, and MOTION ran.\n";
        if (silent.size() > 0) n << "Silent throughout, and not kept: " << silent.joinIntoString (", ") << ".\n";
        p->dir.getChildFile ("PRINT.txt").replaceWithText (n);
        printNote = "PRINTED: " + hmsOf (len) + ", " + String (p->loud.integrated, 1) + " LUFS, PEAK " + String (pk, 1) + " DB.  " + p->dir.getFullPathName().toUpperCase();
    }
    print = nullptr;
    p->ses = nullptr; p->eng = nullptr;                 // its rig is let go before this one sounds again
    if (audioResume) audioResume();
    if (onChange) onChange();
}
} // namespace sf0
