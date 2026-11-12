// SF-000: the app. A window on the screens, the audio device, and three command-line tools:
//   SF-000 --test [--wav file]                 offline audio and timing checks
//   SF-000 --shot file.png [--page library|macros] [--lane N] [--cfg lane|insert|return|space] [--space conv] [--scale 2] [--flat|--dark] [--info [--point x,y]] [--phones]   draw a screen to a PNG
//   SF-000 --dump                              every hosted module's parameters and presets
//   SF-000 --e16 file.oxie16                   write the OXI E16 scene (a page per lane, the snapshot page)
//   SF-000 --miditest                          notes in, the E16's knobs and rings, the clock out: offline checks
//   SF-000 --perf                              the engine's share of real time and what the screen costs, on this machine
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/Processor.h"
#include "core/Registry.h"
#include "core/Bus.h"
#include "core/Sample.h"
#include "Screen.h"
#include "Midi.h"
#include "Recorder.h"
#include "Online.h"
#include "Help.h"
#include <set>

static void dumpModules()
{
    for (auto id : { "KICK", "PERC", "STRIKE", "RUMBLE", "BASS", "LEAD", "PAD", "VOICE", "DELAY", "REVERB", "TERROIR", "DRIVE", "GLUE", "RADIO", "LIMIT", "LOWEND", "TONE" })
    {
        SFProcessor p (sf::makeModule (id));
        std::printf ("== %s params=%d latency=%d\n", id, p.numModuleParams(), p.getLatencySamples());
        const auto& sp = p.allSpecs();
        for (int i = 0; i < (int) sp.size(); ++i)
        {
            const auto& s = sp[(size_t) i];
            std::string ch;
            for (auto& c : s.choices) ch += c + "|";
            std::printf ("  %2d %-14s %-14s t%d %g..%g def %g %s %s%s\n", i, s.id.c_str(), s.name.c_str(), (int) s.type, s.min, s.max, s.def, s.unit.c_str(),
                         s.setup ? "[setup] " : "", ch.c_str());
        }
        std::printf ("  presets:");
        for (auto& pr : p.factoryPresets()) std::printf (" [%s]", pr.name.c_str());
        std::printf ("\n");
    }
}


#include <juce_audio_formats/juce_audio_formats.h>

// Offline checks: every lane sounds, hits land on the grid at any block size, the mix is loud and clean.
static int selfTest (const juce::String& wavPath, double bars = 16.0)
{
    using namespace sf0;
    int bad = 0;
    std::array<float, kLanes> lanePk {};
    auto run = [&] (int block, double bars, std::vector<float>* keep, int solo, float* lufsOut, float* peakOut, std::array<double, kLanes>* laneRms, int* firstOnset)
    {
        Engine e;
        e.prepare (48000.0, block);
        e.solo = solo;
        // two seconds stopped first, as the app is before RUN: every smoother has settled
        { juce::AudioBuffer<float> w (2, block); for (int k = 0; k < 96000 / block; ++k) e.render (w); }
        e.setRunning (true);
        const int total = (int) (bars * 4.0 * 48000.0 * 60.0 / e.tempo());
        juce::AudioBuffer<float> b (2, block);
        float pk = 0.0f; int done = 0, onset = -1; bool nan = false;
        while (done < total)
        {
            const int n = std::min (block, total - done);
            juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, n);
            e.render (v);
            for (int i = 0; i < n; ++i)
            {
                const float l = v.getSample (0, i), r = v.getSample (1, i);
                if (! std::isfinite (l) || ! std::isfinite (r)) nan = true;
                pk = std::max (pk, std::max (std::abs (l), std::abs (r)));
                if (onset < 0 && std::abs (l) > 0.02f) onset = done + i;
                if (keep != nullptr) { keep->push_back (l); keep->push_back (r); }
            }
            done += n;
        }
        if (nan) { std::printf ("  FAIL: non-finite samples (block %d)\n", block); ++bad; }
        if (e.fault.load()) { std::printf ("  FAIL: a module reported a fault (block %d)\n", block); ++bad; }
        if (lufsOut) *lufsOut = e.lufs.load();
        if (peakOut) *peakOut = juce::Decibels::gainToDecibels (pk, -100.0f);
        if (laneRms) for (int l = 0; l < kLanes; ++l) (*laneRms)[(size_t) l] = 10.0 * std::log10 (e.laneSq[(size_t) l] / (2.0 * total) + 1e-12);
        if (firstOnset) *firstOnset = onset;
        if (laneRms) for (int l = 0; l < kLanes; ++l) lanePk[(size_t) l] = juce::Decibels::gainToDecibels (e.laneMax[(size_t) l], -100.0f);
        return e.latencySamples();
    };

    std::printf ("SF-000 offline checks\n");
    std::vector<float> audio;
    float lufs = 0, peak = 0; std::array<double, kLanes> rms {};
    const int lat = run (512, bars, &audio, -1, &lufs, &peak, &rms, nullptr);
    std::printf ("  full rig, %.0f bars at 132: short-term %.1f LUFS at the end, peak %.1f dBFS, lane latency %d samples\n", bars, lufs, peak, lat);
    for (int l = 0; l < kLanes; ++l)
    {
        std::printf ("  lane %2d %-10s %6.1f dB RMS, peak %6.1f dBFS%s\n", l + 1, lanes()[(size_t) l].name, rms[(size_t) l], lanePk[(size_t) l], rms[(size_t) l] < -70.0 ? "   SILENT" : "");
        if (rms[(size_t) l] < -70.0) { std::printf ("  FAIL: lane %d makes no sound\n", l + 1); ++bad; }
    }
    if (lufs < -16.0f || lufs > -6.0f) { std::printf ("  FAIL: loudness out of range\n"); ++bad; }
    if (peak > -0.5f) { std::printf ("  FAIL: peak above the ceiling\n"); ++bad; }

    // No lane holds its sound back, whatever is put on it: the rig's delay does not move with the inserts.
    {
        Engine e;
        e.prepare (48000.0, 128);
        juce::AudioBuffer<float> b (2, 128);
        e.render (b);
        const float base = e.insidePartMs();
        const int held = e.latencySamples();
        int worst = held, least = held;
        float most = base;
        for (int t = 0; t < (int) effects().size(); ++t)
        {
            for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (7, sl, t);
            e.render (b);
            worst = std::max (worst, e.latencySamples()); least = std::min (least, e.latencySamples()); most = std::max (most, e.insidePartMs());
        }
        for (int l = 0; l < kLanes; ++l) for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (l, sl, -1);
        e.render (b);
        const bool steady = lat == held && worst == held && least == held && e.latencySamples() == held && held < 64 && most == base && e.insidePartMs() == base;
        std::printf ("  %s: the rig's own delay is the same with no inserts and with any effect in all %d slots of a lane: %.2f ms (the lanes %d samples, the rest the limiter's look-ahead)\n",
                     steady ? "ok  " : "FAIL", kSlots, base, held);
        if (! steady) ++bad;
    }

    // Recording: the set and its parts go to disk as they are played.
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_rec_test");
        tmp.deleteRecursively();
        const auto state = tmp.getSiblingFile ("sf000_rec_test_state.json");
        state.replaceWithText ("{}");
        auto rd = [] (const juce::File& f, juce::AudioBuffer<float>& into, int& bits, bool& isFloat)
        {
            juce::WavAudioFormat wav;
            auto in = f.createInputStream();
            if (in == nullptr) return false;
            std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (in.release(), true));
            if (r == nullptr) return false;
            bits = (int) r->bitsPerSample; isFloat = r->usesFloatingPointData;
            into.setSize (2, (int) r->lengthInSamples);
            return r->read (&into, 0, (int) r->lengthInSamples, 0, true, true);
        };
        Engine e;
        e.prepare (48000.0, 128);
        e.setPattern (2, Pattern());                           // OPEN HAT plays nothing this take
        juce::AudioBuffer<float> b (2, 128);
        for (int k = 0; k < 750; ++k) e.render (b);            // settled, as the app is
        Recorder rec (e);
        rec.root = tmp;
        rec.stateAtStart = [state] { return state; };
        const auto err = rec.start();
        const bool was = rec.recording();
        e.setRunning (true);
        const int blocks = 1500, total = blocks * 128;         // four seconds
        juce::AudioBuffer<float> played (2, total);
        for (int k = 0; k < blocks; ++k) { e.render (b); for (int ch = 0; ch < 2; ++ch) played.copyFrom (ch, k * 128, b, ch, 0, 128); }
        rec.stop();
        const auto dir = rec.folder();
        const int lead = e.masterLatency();

        juce::AudioBuffer<float> set, stem, sum (2, total);
        sum.clear();
        int bits = 0; bool fl = false;
        bool ok = err.isEmpty() && was && ! rec.recording() && rd (dir.getChildFile ("00 SET.wav"), set, bits, fl) && bits == 24 && ! fl && set.getNumSamples() == total;
        float worst = 0.0f;
        if (ok) for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < total; ++i) worst = std::max (worst, std::abs (set.getSample (ch, i) - played.getSample (ch, i)));
        ok = ok && worst < 2.0f / 8388608.0f;
        std::printf ("  %s: 00 SET.wav is the output, sample for sample (%d samples, 24-bit, furthest %.1e)\n", ok ? "ok  " : "FAIL", set.getNumSamples(), (double) worst);
        if (! ok) ++bad;

        int kept = 0; bool stemsOk = true;
        for (int t = 1; t < Engine::kRecTracks; ++t)
        {
            const auto f = dir.getChildFile (Recorder::trackName (t) + ".wav");
            if (! f.existsAsFile()) continue;
            ++kept;
            stemsOk = stemsOk && rd (f, stem, bits, fl) && bits == 32 && fl && stem.getNumSamples() == total + lead;
            if (stemsOk) for (int ch = 0; ch < 2; ++ch) sum.addFrom (ch, 0, stem, ch, 0, total);
        }
        const bool hatGone = ! dir.getChildFile (Recorder::trackName (3) + ".wav").existsAsFile();
        const auto notes = dir.getChildFile ("take.txt").loadFileAsString();
        stemsOk = stemsOk && kept == Engine::kRecTracks - 2 && hatGone && notes.contains ("NOT KEPT, SILENT THROUGHOUT: 03 OPEN HAT") && notes.contains ("LOST     nothing")
                  && dir.getChildFile ("session-at-start.json").existsAsFile() && rec.lostSeconds() == 0.0;
        std::printf ("  %s: ten lanes and the two returns kept in 32-bit float, each %d samples of silence longer; the lane that played nothing is not kept, and take.txt says so\n",
                     stemsOk ? "ok  " : "FAIL", lead);
        if (! stemsOk) ++bad;

        // The parts, added together, line up with the set. Judged by the top of the spectrum (each
        // signal differenced twice): the master's low cut turns the phase of the bass, which would
        // pull a fit made on everything.
        auto top = [total] (const juce::AudioBuffer<float>& x)
        {
            std::vector<float> y ((size_t) total, 0.0f);
            const float* p = x.getReadPointer (0);
            for (int i = 2; i < total; ++i) y[(size_t) i] = p[i] - 2.0f * p[i - 1] + p[i - 2];
            return y;
        };
        const auto setTop = top (set), sumTop = top (sum);
        int bestLag = 0; double best = -1.0;
        for (int lag = -300; lag <= 300; ++lag)
        {
            double c = 0.0;
            for (int i = 4000; i < total - 4000; ++i) c += (double) setTop[(size_t) i] * sumTop[(size_t) (i + lag)];
            if (c > best) { best = c; bestLag = lag; }
        }
        const bool lined = std::abs (bestLag) <= 2;
        std::printf ("  %s: the lanes and returns added together line up with the set (best fit %d samples off)\n", lined ? "ok  " : "FAIL", bestLag);
        if (! lined) ++bad;

        // a second take is a second folder
        const auto err2 = rec.start();
        for (int k = 0; k < 100; ++k) e.render (b);
        rec.stop();
        const bool two = err2.isEmpty() && rec.folder() != dir && rec.folder().getChildFile ("00 SET.wav").existsAsFile() && dir.getChildFile ("00 SET.wav").existsAsFile();
        std::printf ("  %s: a second take goes in a folder of its own (%s)\n", two ? "ok  " : "FAIL", rec.note().toRawUTF8());
        if (! two) ++bad;
        tmp.deleteRecursively(); state.deleteFile();
    }

    // A snapshot keeps its sound: the last whole bars as a clip, and a profile worked out from them.
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_clip_test");
        tmp.deleteRecursively();
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        juce::int64 kickId = 0, hatId = 0, fullId = 0;
        Profile kick, hat, full;
        bool silentHas = true;
        {
            Engine e;
            e.prepare (48000.0, 128);
            Session ses (e, true);
            juce::AudioBuffer<float> b (2, 128);
            auto bars = [&] (double n) { const int blocks = (int) (n * 4.0 * 60.0 / e.tempo() * 48000.0 / 128.0); for (int k = 0; k < blocks; ++k) e.render (b); };
            for (int k = 0; k < 400; ++k) e.render (b);
            silentHas = ses.library[(size_t) ses.indexOf (ses.take())].profile.has;      // nothing has played: nothing to keep
            auto only = [&] (std::initializer_list<int> keep)
            {
                for (int l = 0; l < kLanes; ++l) ses.userSet (cx::lane (l, cx::kMute), std::find (keep.begin(), keep.end(), l) != keep.end() ? 0.0f : 1.0f);
                bars (6.3);                                                               // taken part of the way through a bar
                return ses.take();
            };
            e.setRunning (true);
            fullId = only ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
            kickId = only ({ 0 });
            hatId = only ({ 1, 2 });
            ses.waitForClips();
            full = ses.library[(size_t) ses.indexOf (fullId)].profile; kick = ses.library[(size_t) ses.indexOf (kickId)].profile; hat = ses.library[(size_t) ses.indexOf (hatId)].profile;

            // the clip: four bars to the sample, beginning on a bar line (the kick's first hit is at its start)
            juce::OggVorbisAudioFormat ogg;
            juce::AudioBuffer<float> clip;
            int first = -1; bool read = false;
            if (auto in = ses.clipFile (kickId).createInputStream())
                if (std::unique_ptr<juce::AudioFormatReader> r { ogg.createReaderFor (in.release(), true) })
                {
                    clip.setSize (2, (int) r->lengthInSamples);
                    read = r->read (&clip, 0, clip.getNumSamples(), 0, true, true);
                    for (int i = 0; i < clip.getNumSamples() && first < 0; ++i) if (std::abs (clip.getSample (0, i)) > 0.05f) first = i;
                }
            const int want = (int) std::lround (16.0 * 60.0 / e.tempo() * 48000.0);
            const bool clipOk = read && std::abs (clip.getNumSamples() - want) <= 2 && first >= 0 && first < 96 && kick.bars == 4 && std::abs (kick.bpm - (float) e.tempo()) < 0.05f;
            std::printf ("  %s: a snapshot's clip is its last four whole bars (%d samples, %d wanted), from the bar line (the kick begins %d samples in), kept as Ogg (%d KB)\n",
                         clipOk ? "ok  " : "FAIL", clip.getNumSamples(), want, first, (int) (ses.clipFile (kickId).getSize() / 1024));
            if (! clipOk) ++bad;

            ses.remove (hatId);
            const bool gone = ! ses.clipFile (hatId).existsAsFile() && ses.clipFile (kickId).existsAsFile();
            std::printf ("  %s: a deleted snapshot's clip goes with it\n", gone ? "ok  " : "FAIL");
            if (! gone) ++bad;
            ses.touch(); ses.save();
        }
        // the profile tells the states apart, and is still there when the app opens again
        const bool four = kick.pulse[0] > 0.5f && kick.pulse[4] > 0.5f && kick.pulse[8] > 0.5f && kick.pulse[12] > 0.5f && kick.pulse[2] < 0.2f && kick.pulse[6] < 0.2f && kick.pulse[10] < 0.2f;
        bool hatLow = true;
        for (int k = 0; k < 16; ++k) hatLow = hatLow && hat.pulse[(size_t) k] < 0.1f;
        const bool told = kick.has && hat.has && full.has && ! silentHas && four && hatLow
                          && kick.weight > 0.8f && hat.weight < 0.1f && hat.bright > 0.8f && kick.bright < 0.3f && kick.tone > hat.tone + 0.5f
                          && full.energy > kick.energy + 0.15f && full.energy > hat.energy + 0.15f
                          && kick.lanes[0] > 0.5f && kick.lanes[1] < 0.05f && hat.lanes[1] > 0.1f && hat.lanes[0] < 0.05f;
        std::printf ("  %s: the profile: the kick alone is heavy, dark and on the four beats; the hats alone are bright with no low end; the whole rig has the most energy (%.2f, %.2f, %.2f); silence keeps nothing\n",
                     told ? "ok  " : "FAIL", kick.energy, hat.energy, full.energy);
        if (! told) ++bad;
        {
            Engine e2;
            e2.prepare (48000.0, 128);
            Session again (e2, true);
            const int i = again.indexOf (kickId);
            const bool back = i >= 0 && again.library[(size_t) i].profile.has && std::abs (again.library[(size_t) i].profile.weight - kick.weight) < 0.01f
                              && std::abs (again.library[(size_t) i].profile.pulse[4] - kick.pulse[4]) < 0.01f && again.clipFile (kickId).existsAsFile();
            std::printf ("  %s: the profile and the clip are there when the app opens again\n", back ? "ok  " : "FAIL");
            if (! back) ++bad;
        }
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
    }

    // A snapshot's sound is the state it holds, not the road to it. Taken as something moves, it
    // waits for bars the rig held still through; the bars before the press are no part of it.
    {
        Engine e;
        e.prepare (48000.0, 128);
        Session ses (e, false);
        juce::AudioBuffer<float> b (2, 128);
        auto bars = [&] (double n) { const int blocks = (int) (n * 4.0 * 60.0 / e.tempo() * 48000.0 / 128.0); for (int k = 0; k < blocks; ++k) e.render (b); };
        auto only = [&] (std::initializer_list<int> keep) { for (int l = 0; l < kLanes; ++l) ses.userSet (cx::lane (l, cx::kMute), std::find (keep.begin(), keep.end(), l) != keep.end() ? 0.0f : 1.0f); };
        auto prof = [&] (juce::int64 id) -> const Profile& { return ses.library[(size_t) ses.indexOf (id)].profile; };
        for (int k = 0; k < 400; ++k) e.render (b);
        e.setRunning (true);
        bars (6.3);
        // held for six bars: its sound is there at once, as before
        const auto a = ses.take();
        const bool atOnce = prof (a).has && prof (a).steady && prof (a).bars == 4 && ses.takesWaiting() == 0 && prof (a).lanes[1] > 0.1f;
        // everything but the kick muted and SNAPSHOT pressed in the same breath: the last four bars were the whole rig
        only ({ 0 });
        const auto k = ses.take();
        const bool waits = ! prof (k).has && ses.takesWaiting() == 1;
        bars (2.0); ses.finishTakes();
        const bool still = ! prof (k).has && ses.takesWaiting() == 1;
        bars (4.6); ses.finishTakes();
        const bool kickOnly = prof (k).has && prof (k).steady && prof (k).bars == 4 && prof (k).lanes[0] > 0.5f && prof (k).lanes[1] < 0.05f && prof (k).lanes[5] < 0.05f
                              && prof (k).weight > 0.8f && ses.takesWaiting() == 0;
        std::printf ("  %s: a snapshot taken the moment the rig changes waits for four bars held in its state (kick alone: kick %.2f, hat %.2f, bass %.2f; the bars before the press had hat %.2f); one taken after six held bars has its sound at once\n",
                     atOnce && waits && still && kickOnly ? "ok  " : "FAIL", prof (k).lanes[0], prof (k).lanes[1], prof (k).lanes[5], prof (a).lanes[1]);
        if (! (atOnce && waits && still && kickOnly)) ++bad;

        // moved on after a few bars: what was held of it, fewer than four
        only ({ 1, 2 });
        const auto h = ses.take();
        bars (3.4);
        only ({ 0 });
        bars (0.3); ses.finishTakes();
        const bool part = prof (h).has && prof (h).steady && prof (h).bars >= 1 && prof (h).bars < 4 && prof (h).lanes[1] > 0.1f && prof (h).lanes[0] < 0.05f && ses.takesWaiting() == 0;
        // moved on at once: nothing of it was held, so the bars it was taken over, marked as such
        bars (6.0);
        only ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
        const auto m = ses.take();
        bars (0.2);
        only ({ 0 });
        bars (0.2); ses.finishTakes();
        const bool fell = prof (m).has && ! prof (m).steady && ses.takesWaiting() == 0;
        std::printf ("  %s: if the rig moves on before four bars are held, the snapshot takes the bars that were (%d, the hats alone); if none were, the bars it was taken over, and it says so\n",
                     part && fell ? "ok  " : "FAIL", prof (h).bars);
        if (! (part && fell)) ++bad;

        // a knob still travelling when SNAPSHOT is pressed: the sound kept is where it arrives
        only ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
        bars (6.0);
        const int steps = e.control (cx::kGlide).steps();
        for (int i = 0; i < steps; ++i) if (glideBarsFor ((float) i / (float) (steps - 1)) == 2.0) ses.userSet (cx::kGlide, (float) i / (float) (steps - 1));
        const auto before = ses.take();
        ses.userSet (cx::lane (1, kLaneKnobs - 1), 0.0f);          // the closed hat's LEVEL, down over two bars
        const auto g = ses.take();
        bars (9.5); ses.finishTakes();
        const bool arrived = prof (before).has && prof (before).lanes[1] > 0.1f && prof (g).has && prof (g).steady && prof (g).lanes[1] < 0.05f && prof (g).lanes[0] > 0.5f && ses.takesWaiting() == 0;
        std::printf ("  %s: taken as a knob sets off on a two-bar glide, the sound kept is the rig once it has arrived (the hat's lane %.2f before, %.2f kept)\n",
                     arrived ? "ok  " : "FAIL", prof (before).lanes[1], prof (g).lanes[1]);
        if (! arrived) ++bad;
    }

    // A recall crosses over what cannot travel. An effect put in or taken out, a model, a menu: the
    // one that was there carries on as it is and fades out over the GLIDE time while the new one
    // arrives beside it. What can travel still travels, the knobs of an effect that stays among them.
    {
        struct Tap : Engine::RecSink
        {
            int want = 0; std::vector<float> x;
            void begin (int) noexcept override {}
            void track (int index, const float* l, const float*, int n) noexcept override { if (index == want) x.insert (x.end(), l, l + n); }
            void end (int) noexcept override {}
        };
        Engine e;
        e.prepare (48000.0, 128);
        Session ses (e, false);
        juce::AudioBuffer<float> b (2, 128);
        const double perBar = 4.0 * 60.0 / e.tempo() * 48000.0;
        auto bars = [&] (double n) { const int blocks = (int) (n * perBar / 128.0); for (int k = 0; k < blocks; ++k) e.render (b); };
        auto jump = [] (const std::vector<float>& x, size_t from, size_t to) { float m = 0.0f; for (size_t i = std::max<size_t> (1, from); i < std::min (to, x.size()); ++i) m = std::max (m, std::abs (x[i] - x[i - 1])); return m; };
        const int steps = e.control (cx::kGlide).steps();
        for (int i = 0; i < steps; ++i) if (glideBarsFor ((float) i / (float) (steps - 1)) == 2.0) ses.userSet (cx::kGlide, (float) i / (float) (steps - 1));
        for (int k = 0; k < 400; ++k) e.render (b);
        e.setRunning (true);
        bars (2.0);

        // -- effects: the lead comes with DELAY and RADIO; another state has DISTORTION in the delay's place and no radio
        const auto withDelay = ses.take();
        e.setInsertType (7, 0, 3);
        e.setInsertType (7, 1, -1);
        bars (0.5);
        const auto withDist = ses.take();
        ses.finishTakes (true);
        ses.recall (withDelay);
        bars (3.5); e.tidy(); bars (0.1); e.tidy();
        const bool settled = e.leaving() == 0 && e.insertType (7, 0) == 0 && e.insertType (7, 1) == 5;
        Tap tap; tap.want = 1 + 7;
        e.setRecSink (&tap);
        bars (2.0);
        const size_t at = tap.x.size();
        ses.recall (withDist);
        const bool both = e.leaving() == 2 && e.insertType (7, 0) == 3 && e.insertType (7, 1) == -1;
        bars (1.25);
        const float mid = e.fadeShown (7, 0), midB = e.fadeShown (7, 1);
        bars (1.25);
        const float done = e.fadeShown (7, 0);
        const size_t after = tap.x.size();
        e.tidy(); bars (0.1); e.tidy();
        const float calm = jump (tap.x, 0, at), across = jump (tap.x, at, after);
        e.setRecSink (nullptr);
        const bool crossed = settled && both && mid > 0.3f && mid < 0.7f && std::abs (mid - midB) < 1.0e-6f && done == 1.0f && e.leaving() == 0 && across < calm * 2.0f + 0.01f;
        std::printf ("  %s: a recall that changes a lane's effects crosses them over the GLIDE time: both run, half way after a bar and a beat (%.2f), done in two bars and a beat, the old ones let go, and no step in the lane's sound (largest %.3f, against %.3f before)\n",
                     crossed ? "ok  " : "FAIL", mid, across, calm);
        if (! crossed) ++bad;

        // -- an effect that stays: its knobs travel
        const int fb = cx::ins (7, 0, 1);                           // the distortion's CHARACTER
        ses.userSet (fb, 0.2f); bars (3.0);
        const auto low = ses.take();
        ses.userSet (fb, 0.9f); bars (3.0);
        const auto high = ses.take();
        ses.finishTakes (true);
        ses.recall (low);
        const int none = e.leaving();
        bars (1.25);
        const float part = e.shown (fb);
        bars (1.5);
        const bool travelled = none == 0 && part > 0.3f && part < 0.8f && std::abs (e.shown (fb) - 0.2f) < 1.0e-4f;
        juce::ignoreUnused (high);

        // -- the instrument: a model changed in CONFIG, a menu turned, and a knob that only travels
        Tap kick; kick.want = 1;
        auto st = e.getSetup ({ 0, 0, 0 });
        const auto first = st;
        for (auto& kv : st) if (kv.first == "model") kv.second = kv.second < 0.5f ? 1.0f : 0.0f;
        const auto plain = ses.take();                              // the kick as it comes
        e.setRecSink (&kick);
        bars (2.0);
        const size_t kA = kick.x.size();                            // (two bars of the one model, then two of the other)
        e.applySetup ({ 0, 0, 0 }, st);
        bars (0.5);
        const auto otherModel = ses.take();
        ses.finishTakes (true);
        bars (2.0);
        const size_t k0 = kick.x.size();
        ses.recall (plain);
        const bool forked = e.leaving() == 1;
        bars (1.25);
        const float vmid = e.fadeShown (0, kSlots);
        bars (1.5);
        const size_t k1 = kick.x.size();
        e.tidy(); bars (0.1); e.tidy();
        e.setRecSink (nullptr);
        bool back = e.leaving() == 0 && e.fadeShown (0, kSlots) == 1.0f;
        for (auto& kv : e.getSetup ({ 0, 0, 0 })) for (auto& was : first) if (kv.first == was.first) back = back && std::abs (kv.second - was.second) < 1.0e-4f;
        // every beat of the crossing has its kick: neither silence nor a doubled hit
        // every beat of the crossing has its kick, between the two models' own levels: neither a gap nor a doubled hit
        float least = 10.0f, most = 0.0f, one = 0.0f, other = 0.0f;
        const size_t beat = (size_t) (perBar / 4.0);
        auto peaks = [&] (size_t from, size_t to, float& lo, float& hi)
        {
            for (size_t p = from; p + beat <= to; p += beat) { float pk = 0.0f; for (size_t i = p; i < p + beat; ++i) pk = std::max (pk, std::abs (kick.x[i])); lo = std::min (lo, pk); hi = std::max (hi, pk); }
        };
        float skip = 10.0f;
        peaks (0, kA, skip, one);
        peaks (k0 - 2 * (size_t) perBar, k0, skip, other);
        peaks (k0, k1, least, most);
        const float usual = std::max (one, other), quiet = std::min (one, other);
        const bool steady = quiet > 0.02f && least > 0.6f * quiet && most < 1.25f * usual;
        // a menu on the lane (the kick's ECHO SPACE) forks too; a knob that travels does not
        const int menu = cx::lane (0, 2), tune = cx::lane (0, 0);
        const auto m0 = ses.take();
        ses.userSet (menu, e.target (menu) < 0.5f ? 1.0f : 0.0f);
        const auto m1 = ses.take();
        ses.userSet (tune, 0.7f); bars (3.0);
        const auto t1 = ses.take();
        ses.finishTakes (true);
        ses.recall (m1);                                            // only TUNE differs: it glides
        const int glides = e.leaving();
        bars (3.0);
        ses.recall (m0);                                            // the menu differs: the lane is forked
        const int forks = e.leaving();
        bars (3.0); e.tidy(); bars (0.1); e.tidy();
        juce::ignoreUnused (otherModel, t1, kA);
        const bool told = glides == 0 && forks == 1 && e.leaving() == 0;
        std::printf ("  %s: a kept effect's knobs travel through a recall (%.2f on the way from 0.90 to 0.20); a lane whose model or menu changes is crossed from the one playing to its changed copy (%.2f after a bar and a beat) with a kick on every beat of it (beats peak between %.2f and %.2f; the two models alone %.2f and %.2f), and one where only knobs differ is left to glide\n",
                     travelled && forked && vmid > 0.3f && vmid < 0.7f && back && steady && told ? "ok  " : "FAIL", part, vmid, least, most, quiet, usual);
        if (! (travelled && forked && vmid > 0.3f && vmid < 0.7f && back && steady && told)) ++bad;

        // what a fork costs the message thread (the audio thread is not held up by it)
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int l = 0; l < kLanes; ++l) e.forkVoice (l, 0.0);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        bars (0.5); e.tidy(); bars (0.1); e.tidy();
        std::printf ("  %s: forking all eleven lanes at once takes %.0f ms of the message thread, and all are let go again\n", e.leaving() == 0 ? "ok  " : "FAIL", ms);
        if (e.leaving() != 0) ++bad;
    }

    // Cue: a snapshot's clip in the headphones, in time with the clock, and the set untouched by it.
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_cue_test");
        tmp.deleteRecursively();
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        {
            // The set is compared with a second rig run beside it. The two share the suite's bus, and
            // its instruments judge how old a kick's report is by the clock on the wall; with the
            // render pausing (the clip is written here) the two could come to differ by that alone.
            // So for these two the clock is the time rendered, the same for both in every buffer.
            static double rendered = 0.0;
            rendered = 1000.0;
            sf::Bus::testClock = +[] { return rendered; };
            Engine e, plain;
            e.prepare (48000.0, 128); plain.prepare (48000.0, 128);
            Session ses (e, true);
            juce::AudioBuffer<float> b (2, 128), pb (2, 128);
            auto both = [&] { rendered += 128.0 / 48000.0; e.render (b); plain.render (pb); };
            for (int k = 0; k < 400; ++k) both();
            e.setRunning (true); plain.setRunning (true);
            const int perBar = (int) (4.0 * 60.0 / e.tempo() * 48000.0 / 128.0);
            for (int k = 0; k < perBar * 6; ++k) both();
            const auto id = ses.take();
            ses.waitForClips();
            // the clip as the cue will play it
            juce::AudioBuffer<float> clip;
            {
                juce::OggVorbisAudioFormat ogg;
                if (auto in = ses.clipFile (id).createInputStream())
                    if (std::unique_ptr<juce::AudioFormatReader> r { ogg.createReaderFor (in.release(), true) })
                    { clip.setSize (2, (int) r->lengthInSamples); r->read (&clip, 0, clip.getNumSamples(), 0, true, true); }
            }
            const int len = clip.getNumSamples();
            const bool began = len > 1000 && ses.cue (id) && ses.cueing() == id;
            // in time: each buffer's first sample is the clip where the rig is in its four bars
            double worst = 0.0, loud = 0.0, drift = 0.0; bool same = true;
            const double held = (double) (e.latencySamples() + e.masterLatency());
            for (int k = 0; k < perBar * 5 && began; ++k)
            {
                const double ppq = e.songPos();
                both();
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) drift = std::max (drift, (double) std::abs (b.getSample (ch, i) - pb.getSample (ch, i)));
                same = drift == 0.0;
                if (k < 8) continue;                                        // (it fades in)
                const double perSample = e.tempo() / 60.0 / 48000.0;
                double q = std::fmod (ppq - held * perSample, 16.0); if (q < 0.0) q += 16.0;
                const double pos = q / 16.0 * (double) len;
                const int i0 = juce::jlimit (0, len - 1, (int) pos), i1 = (i0 + 1) % len;
                const double want = clip.getSample (0, i0) + (clip.getSample (0, i1) - clip.getSample (0, i0)) * (pos - (double) i0);
                worst = std::max (worst, std::abs (want - (double) e.cueBlock().getSample (0, 0)));
                loud = std::max (loud, (double) e.cueBlock().getMagnitude (0, 128));
            }
            const bool timed = began && worst < 2.0e-3 && loud > 0.1;
            std::printf ("  %s: CUE plays a snapshot's clip in time with the clock: each buffer begins where the rig is in its four bars (furthest %.1e), and the set is the same to the last bit with it playing (%.1e from a rig run beside it)\n",
                         timed && same ? "ok  " : "FAIL", worst, drift);
            if (! (timed && same)) ++bad;

            // stopped, it plays as it was kept; taken away, it fades out and the clip is let go
            e.setRunning (false); plain.setRunning (false);
            for (int k = 0; k < 40; ++k) both();
            float pk = 0.0f;
            for (int k = 0; k < 40; ++k) { both(); pk = std::max (pk, e.cueBlock().getMagnitude (0, 128)); }
            ses.cue (0);
            for (int k = 0; k < 8; ++k) both();
            float after = 0.0f;
            for (int k = 0; k < 40; ++k) { both(); after = std::max (after, e.cueBlock().getMagnitude (0, 128)); }
            const bool off = pk > 0.1f && after == 0.0f && ses.cueing() == 0 && ! ses.cue (ses.library.front().id);       // (CLEAN SLATE has no clip)
            std::printf ("  %s: with the clock stopped it plays on as it was kept; taken away it is silent within a few buffers; a snapshot with no sound kept gives none\n", off ? "ok  " : "FAIL");
            if (! off) ++bad;
            {
                // a clip kept at another rate (the interface ran at 96 kHz then, 48 now): stopped, it plays at its own speed
                Engine e4; e4.prepare (48000.0, 128);
                Engine::CueClip c4; c4.rate = 96000.0; c4.audio.setSize (2, 96000);
                for (int i = 0; i < 96000; ++i) { const float v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) i / 96000.0); c4.audio.setSample (0, i, v); c4.audio.setSample (1, i, v); }
                e4.setCue (&c4);
                juce::AudioBuffer<float> b4 (2, 128);
                for (int k4 = 0; k4 < 20; ++k4) e4.render (b4);
                int crossings = 0; float was = 0.0f;
                for (int k4 = 0; k4 < 375; ++k4)                        // one second
                {
                    e4.render (b4);
                    for (int i = 0; i < 128; ++i) { const float v = e4.cueBlock().getSample (0, i); if (was <= 0.0f && v > 0.0f) ++crossings; was = v; }
                }
                e4.setCue (nullptr);
                for (int k4 = 0; k4 < 20; ++k4) e4.render (b4);
                const bool pitch = std::abs (crossings - 440) <= 1;
                std::printf ("  %s: a clip kept at 96 kHz, played stopped on an interface at 48: an A at 440 is heard at %d (it was an octave down)\n", pitch ? "ok  " : "FAIL", crossings);
                if (! pitch) ++bad;
            }
            {
                // four bars at 96 kHz and 90: more than the tape used to hold (a snapshot's bars then never came)
                Engine e5; e5.prepare (96000.0, 128);
                e5.set (cx::kTempo, tempoToValue (90.0));
                juce::AudioBuffer<float> b5 (2, 128);
                e5.setRunning (true);
                const int blocks = (int) (7.3 * 4.0 * 60.0 / 90.0 * 96000.0 / 128.0);       // (the first bars after RUN are not yet held still)
                for (int k5 = 0; k5 < blocks; ++k5) e5.render (b5);
                Engine::Heard h5;
                const bool some = e5.heldHeard (h5, e5.stateStamp());
                const bool four = some && h5.bars == 4 && h5.most == 4 && h5.audio.getNumSamples() == 1024000;
                std::printf ("  %s: at 96 kHz and 90 a snapshot's four bars are all on the tape (%d bars, %d samples; the tape held 983040)\n", four ? "ok  " : "FAIL", h5.bars, h5.audio.getNumSamples());
                if (! four) ++bad;
                // a recording under way when the audio starts again at another rate is fed nothing more
                struct Count : Engine::RecSink { void begin (int n) noexcept override { frames += n; } void track (int, const float*, const float*, int) noexcept override {} void end (int) noexcept override {} int frames = 0; } sink;
                e5.setRecSink (&sink);
                for (int k5 = 0; k5 < 10; ++k5) e5.render (b5);
                const int before = sink.frames;
                e5.prepare (96000.0, 128);                              // the same rate: it carries on
                for (int k5 = 0; k5 < 10; ++k5) e5.render (b5);
                const int same = sink.frames;
                e5.prepare (48000.0, 128);                              // another: let go of
                for (int k5 = 0; k5 < 10; ++k5) e5.render (b5);
                const bool rec = before == 1280 && same == 2560 && sink.frames == 2560 && ! e5.hasRecSink();
                std::printf ("  %s: a recording is fed nothing once the audio has started again at another rate (%d frames, then %d, then %d): no stretch of it plays at the wrong speed\n", rec ? "ok  " : "FAIL", before, same, sink.frames);
                if (! rec) ++bad;
                e5.setRecSink (nullptr);
                // a sample already read and still held is shared, not read from the disk again; and is not lost with the disk
                {
                    const auto f6 = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_shared_sample.wav");
                    f6.deleteFile();
                    {
                        juce::AudioBuffer<float> w6 (2, 96000);
                        for (int i = 0; i < 96000; ++i) { const float v = 0.4f * (float) std::sin (0.02 * (double) i); w6.setSample (0, i, v); w6.setSample (1, i, v); }
                        juce::WavAudioFormat wav;
                        if (auto out = f6.createOutputStream())
                            if (std::unique_ptr<juce::AudioFormatWriter> wr { wav.createWriterFor (out.get(), 48000.0, 2, 16, {}, 0) }) { out.release(); wr->writeFromAudioSampleBuffer (w6, 0, 96000); }
                    }
                    const double t0 = juce::Time::getMillisecondCounterHiRes();
                    auto a6 = sf::loadSampleFile (f6, 48000.0, 300.0);
                    const double t1 = juce::Time::getMillisecondCounterHiRes();
                    auto b6 = sf::loadSampleFile (f6, 48000.0, 300.0);
                    const double t2 = juce::Time::getMillisecondCounterHiRes();
                    auto other = sf::loadSampleFile (f6, 44100.0, 300.0);            // (at another rate it is another sample)
                    f6.deleteFile();
                    auto c6 = sf::loadSampleFile (f6, 48000.0, 300.0);                // the file gone, the sample still held
                    const bool shared = a6 != nullptr && a6.get() == b6.get() && c6.get() == a6.get() && other != nullptr && other.get() != a6.get();
                    a6.reset(); b6.reset(); c6.reset();
                    const bool gone = sf::loadSampleFile (f6, 48000.0, 300.0) == nullptr;       // held by no one now, and no file: nothing
                    std::printf ("  %s: a sample already read is shared, not read again (%.1f ms the first time, %.3f ms the second), and outlives its file while a voice holds it\n", shared && gone ? "ok  " : "FAIL", t1 - t0, t2 - t1);
                    if (! (shared && gone)) ++bad;
                }
                // MOTION: a wave moving one of an instrument's knobs, set in its CONFIG and kept in snapshots
                {
                    Engine e7; e7.prepare (48000.0, 128);
                    Session s7 (e7, false);
                    juce::AudioBuffer<float> b7 (2, 128);
                    const Engine::Target bass { 0, 5, 0 };
                    auto* pr = e7.processorFor (bass);
                    const int cutoff = pr->paramIndex ("cutoff");
                    auto params = [&] { return e7.setupParams (bass); };
                    auto find = [&] (const juce::String& id) { for (auto& sp : params()) if (sp.id == id) return sp; return Engine::SetupParam(); };
                    auto choose = [&] (const juce::String& id, const juce::String& name)
                    {
                        const auto sp = find (id);
                        const int at = sp.choices.indexOf (name);
                        if (at >= 0 && sp.choices.size() > 1) e7.setSetup (bass, id, (float) at / (float) (sp.choices.size() - 1));
                        return at >= 0 && find (id).text.toUpperCase() == name;
                    };
                    auto spread = [&] (int blocks)
                    {
                        float lo = 1.0e9f, hi = -1.0e9f;
                        for (int n7 = 0; n7 < blocks; ++n7) { e7.render (b7); const float v = pr->currentValue (cutoff); lo = std::min (lo, v); hi = std::max (hi, v); }
                        return hi - lo;                                             // (CUTOFF as the instrument hears it, 0 to 1)
                    };
                    e7.setRunning (true);
                    const float still = spread (400);
                    const auto t0 = find ("m1_target");
                    const bool listed = t0.group == "MOTION 1" && t0.name == "TARGET" && t0.choices.size() > 12 && t0.choices[0] == "OFF" && t0.choices[1] == juce::String (lanes()[5].knobs[0].label);
                    const bool set = choose ("m1_src", "SINE") && choose ("m1_rate", "1/4") && choose ("m1_target", "CUTOFF");
                    e7.setSetup (bass, "m1_depth", 1.0f);
                    const float moving = spread (800);
                    // kept in a snapshot, and brought back by it; a snapshot from before MOTION says nothing of it and means it off
                    const auto with = s7.take();
                    s7.finishTakes (true);
                    e7.setSetup (bass, "m1_src", 0.0f);
                    const float off = spread (400);
                    s7.recall (with);
                    for (int n7 = 0; n7 < 400; ++n7) e7.render (b7);
                    pr = e7.processorFor (bass);                                    // (the recall crossed the voice over: its instrument is another)
                    const float back = spread (800);
                    const bool wave = find ("m1_src").text.toUpperCase() == "SINE" && find ("m1_target").text.toUpperCase() == "CUTOFF";
                    for (auto& snap : s7.library)
                        if (snap.id == with)
                        {
                            auto& st = snap.setup[5];
                            st.erase (std::remove_if (st.begin(), st.end(), [] (const auto& kv) { return Engine::isMotionId (kv.first); }), st.end());
                        }
                    s7.recall (with);
                    for (int n7 = 0; n7 < 400; ++n7) e7.render (b7);
                    pr = e7.processorFor (bass);
                    const float before = spread (400);
                    const bool ok7 = listed && set && still < 0.01f && moving > 0.3f && off < 0.01f && back > 0.3f && wave && before < 0.01f && find ("m1_src").text.toUpperCase() == "OFF";
                    std::printf ("  %s: MOTION: a sine set on the bass's CUTOFF in its CONFIG moves what the instrument hears (it travels %.2f of its range with no motion, %.2f with it, %.2f switched off), is kept in a snapshot and brought back by it (%.2f), and a snapshot from before MOTION puts it off (%.2f)\n",
                                 ok7 ? "ok  " : "FAIL", still, moving, off, back, before);
                    if (! ok7) { ++bad; std::printf ("    (listed %d set %d wave %d: %s)\n", (int) listed, (int) set, (int) wave, t0.choices.joinIntoString (", ").toRawUTF8()); }
                }
            }
        }
        sf::Bus::testClock = nullptr;
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
    }

    // The session file (FORMAT.md): one written by an older build opens as it did and loses nothing;
    // whatever this build does not know in a file is carried through a save; every snapshot says
    // what it was made with; a file in a newer format is left alone.
    {
        using juce::var;
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_format_test");
        tmp.deleteRecursively();
        tmp.createDirectory();
        const auto state = tmp.getChildFile ("session.json");
        Session::stateFileForTests = state;
        auto text = [] (const var& v) { return juce::JSON::toString (v, true); };
        auto without = [] (const var& v, const char* key) { var c = v.clone(); if (auto* o = c.getDynamicObject()) o->removeProperty (key); return c; };
        auto open = [] (const std::function<void (Session&)>& with)
        {
            Engine e;
            e.prepare (48000.0, 128);
            Session ses (e, true);
            with (ses);
            ses.touch(); ses.save();
        };
        auto isUid = [] (const juce::String& u) { return u.length() > 17 && u[16] == '-' && u.substring (0, 16).containsOnly ("0123456789abcdef") && u.substring (17).containsOnly ("0123456789abcdef"); };

        const juce::File fixture = juce::File (SF000_SOURCE_DIR).getChildFile ("tests").getChildFile ("fixtures").getChildFile ("session-2026-11-11.json");
        if (! fixture.existsAsFile())
            std::printf ("  SKIP: the checks on an older build's session file need %s\n", fixture.getFullPathName().toRawUTF8());
        else
        {
            // (a) a file as the build of 11 November 2026 wrote it, before any of this was kept
            const var old = juce::JSON::parse (fixture);
            const int n = old["library"].size();
            fixture.copyFileTo (state);
            bool read = false; juce::String libId;
            open ([&] (Session& ses)
            {
                const int up = ses.indexOf (1001), last = (int) ses.library.size() - 1;
                read = (int) ses.library.size() == n && up >= 0 && ses.library[(size_t) up].name == "ENERGY UP"
                       && std::abs (ses.library[(size_t) up].values[(size_t) ses.engine.find ("closed_hat.density")] - 0.45f) < 1.0e-6f
                       && ses.library[(size_t) up].inserts[7][0] == 0 && ses.library[(size_t) up].inserts[7][1] == 5 && ses.library[(size_t) up].inserts[7][2] == -1
                       && ses.library[(size_t) last].name == old["library"][n - 1]["name"].toString() && ses.library[(size_t) last].profile.has
                       && std::abs (ses.library[(size_t) last].profile.weight - (float) (double) old["library"][n - 1]["profile"]["weight"]) < 1.0e-3f
                       && ses.notSavingWhy.isEmpty();
                for (auto& sn : ses.library) read = read && ses.findSnap (sn.id) != nullptr;
                libId = ses.libraryId;
            });
            var now = juce::JSON::parse (state);
            bool same = now["library"].size() == n, signedAll = true;
            for (int i = 0; i < n && same; ++i)
            {
                signedAll = signedAll && isUid (now["library"][i]["uid"].toString()) && now["library"][i]["uid"].toString().startsWith (libId + "-");
                if ((juce::int64) old["library"][i]["id"] == kCleanSlateId) continue;      // made again each time the app opens
                same = text (without (now["library"][i], "uid")) == text (old["library"][i]);
            }
            const auto uid5 = now["library"][5]["uid"].toString();
            open ([] (Session&) {});
            now = juce::JSON::parse (state);
            const bool steady = now["library"][5]["uid"].toString() == uid5 && now["libraryId"].toString() == libId
                                && tmp.getChildFile ("library-id.txt").loadFileAsString().trim() == libId;
            tmp.getChildFile ("library-id.txt").deleteFile();                               // (its own file lost: the session's copy serves)
            open ([] (Session&) {});
            const bool found = tmp.getChildFile ("library-id.txt").loadFileAsString().trim() == libId;
            std::printf ("  %s: a session file from the build of 11 November opens as it did (%d snapshots), and is saved with every snapshot as it was to the letter, each now with a uid that stays (%s)\n",
                         read && same && signedAll && steady && found ? "ok  " : "FAIL", n, uid5.toRawUTF8());
            if (! (read && same && signedAll && steady && found)) ++bad;

            // (b) the same file with things in it this build has never heard of, at every depth
            var odd = old.clone();
            odd.getDynamicObject()->setProperty ("futureThing", juce::JSON::parse (R"({"a": 1, "b": [1, 2, 3]})"));
            odd["controls"].getDynamicObject()->setProperty ("g.future", 0.5);
            odd["knobs"][0].getDynamicObject()->setProperty ("curve", "exp");
            { auto* d = new juce::DynamicObject(); d->setProperty ("00000000deadbeef", juce::JSON::parse (R"({"kind": "lane", "slot": "kick", "name": "A KICK LONG GONE"})")); odd.getDynamicObject()->setProperty ("defs", var (d)); }
            var sn = odd["library"][n - 2];
            const auto snId = (juce::int64) sn["id"];
            sn.getDynamicObject()->setProperty ("ext", juce::JSON::parse (R"({"web": {"tags": ["dark", "rolling"], "like": [0.25, 0.5]}})"));
            sn.getDynamicObject()->setProperty ("somethingNew", 5);
            sn["values"].getDynamicObject()->setProperty ("kick.future_knob", 0.42);
            sn["values"].getDynamicObject()->setProperty ("newlane.tune", 0.3);
            sn["inserts"][5].getArray()->set (2, "SHIMMER");
            sn["insertFiles"].getArray()->add (juce::JSON::parse (R"({"lane": 5, "slot": 2, "files": [{"place": 0, "type": 9, "files": ["/somewhere/a.wav"]}]})"));
            sn["profile"].getDynamicObject()->setProperty ("tension", 0.7);
            sn["profile"].getDynamicObject()->setProperty ("v", 9);
            bool stepped = false;
            if (auto* st = sn["patterns"][0]["steps"].getArray(); st != nullptr && st->size() > 0) { st->getReference (0).getArray()->add (0.5); stepped = true; }
            state.replaceWithText (juce::JSON::toString (odd));
            bool inApp = false;
            open ([&] (Session& ses)
            {
                const int i = ses.indexOf (snId);
                inApp = i >= 0 && ses.library[(size_t) i].inserts[5][2] == -1 && ses.library[(size_t) i].profile.has
                        && ses.library[(size_t) i].patterns[0].steps[0].on == stepped;
                ses.rename (snId, "roller");
            });
            now = juce::JSON::parse (state);
            var want = sn.clone();
            want.getDynamicObject()->setProperty ("name", "ROLLER");
            const bool carried = text (without (now["library"][n - 2], "uid")) == text (want)
                                 && text (now["futureThing"]) == text (odd["futureThing"])
                                 && (double) now["controls"]["g.future"] == 0.5 && now["knobs"][0]["curve"].toString() == "exp"
                                 && now["defs"]["00000000deadbeef"]["name"].toString() == "A KICK LONG GONE" && now["defs"].getDynamicObject()->getProperties().size() > 20
                                 && stepped && inApp;
            std::printf ("  %s: what this build does not know in a file is carried through a save: keys of the file's own, a tool's notes on a snapshot, values for knobs and an effect it has not got, more in a profile and a step, a definition long gone; and the snapshot's new name is in\n",
                         carried ? "ok  " : "FAIL");
            if (! carried) ++bad;
        }

        // (c) a fresh library: every snapshot says what it was made with, and the file holds the definitions
        tmp.deleteRecursively(); tmp.createDirectory();
        juce::int64 took = 0;
        open ([&] (Session& ses) { took = ses.take(); });
        {
            const var now = juce::JSON::parse (state);
            const var lib = now["library"];
            var sn;
            for (int i = 0; i < lib.size(); ++i) if ((juce::int64) lib[i]["id"] == took) sn = lib[i];
            const var made = sn["made"], defs = now["defs"];
            auto has = [&defs] (const var& print, const char* kind) { return print.toString().length() == 16 && defs[juce::Identifier (print.toString())]["kind"].toString() == kind; };
            bool ok = sn.isObject() && sn["uid"].toString() == now["libraryId"].toString() + "-" + juce::String::toHexString (took) && isUid (sn["uid"].toString())
                      && made["slots"].size() == kLanes && made["app"].toString().startsWith (JUCE_APPLICATION_VERSION_STRING)
                      && has (made["chassis"], "chassis") && has (made["returns"]["dub"], "return") && has (made["returns"]["space"], "return") && has (made["returns"]["conv"], "return")
                      && has (made["fx"]["DELAY"], "fx") && has (made["fx"]["RADIO"], "fx") && made["fx"].getDynamicObject()->getProperties().size() == 2
                      && (int) now["sf000"] == Session::kFormat && now["wrote"]["app"].toString().isNotEmpty();
            for (int l = 0; l < kLanes && ok; ++l)
            {
                const auto slot = made["slots"][l].toString();
                const var def = defs[juce::Identifier (made["lanes"][juce::Identifier (slot)].toString())];
                ok = slot == lanes()[(size_t) l].slug && def["kind"].toString() == "lane" && def["slot"].toString() == slot && def["knobs"].size() == kLaneKnobs
                     && has (def["engine"], "engine") && defs[juce::Identifier (def["engine"].toString())]["params"][juce::Identifier (def["knobs"][0]["param"].toString())].isObject();
            }
            // the same build gives the same prints every time, and a profile says how it was worked out
            Engine a, b;
            bool again = a.defs().all.size() == b.defs().all.size() && a.defs().chassis == b.defs().chassis;
            for (int l = 0; l < kLanes; ++l) again = again && a.defs().lane[(size_t) l] == b.defs().lane[(size_t) l];
            Profile pr; pr.has = true;
            const bool pv = (int) pr.toVar()["v"] == kProfileVersion;
            std::printf ("  %s: a snapshot says what it was made with (the build, the lanes in order, a print for each lane, effect in use, return and the chassis), and the file holds every definition a print names (%d of them, %d KB)\n",
                         ok && again && pv ? "ok  " : "FAIL", defs.getDynamicObject() != nullptr ? defs.getDynamicObject()->getProperties().size() : 0, (int) (text (defs).length() / 1024));
            if (! (ok && again && pv)) ++bad;
        }

        // (d) a file in a newer format is not read and not written over; one that is not a session is put aside first
        tmp.deleteRecursively(); tmp.createDirectory();
        const juce::String newer = R"({"sf000": 2, "shape": "something this build cannot read", "library": []})";
        state.replaceWithText (newer);
        bool said = false, factory = false;
        open ([&] (Session& ses) { said = ses.notSavingWhy.isNotEmpty(); factory = ses.indexOf (1000) >= 0; ses.take(); });
        const bool left = said && factory && state.loadFileAsString() == newer && tmp.getNumberOfChildFiles (juce::File::findFilesAndDirectories, "*") == 2;   // (the file, and its backup folder)
        tmp.deleteRecursively(); tmp.createDirectory();
        state.replaceWithText ("{ \"sf000\": 1, \"library\": [ { \"id\": 5, ");               // cut short
        open ([] (Session&) {});
        const auto aside = tmp.findChildFiles (juce::File::findFiles, false, "session-unreadable-*.json");
        const bool put = aside.size() == 1 && aside[0].loadFileAsString().startsWith ("{ \"sf000\": 1, \"library\": [ { \"id\": 5") && (int) juce::JSON::parse (state)["sf000"] == Session::kFormat;
        std::printf ("  %s: a session file in a newer format is left exactly as it is (nothing is written while it is there, and the screen says so); one cut short is put aside before a fresh one replaces it\n",
                     left && put ? "ok  " : "FAIL");
        if (! (left && put)) ++bad;
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
    }

    // Stopped, the rig is silent. The lead's RADIO (and the VOICE's own) used to hiss on with nothing
    // playing, and the radio's receiver chirped to itself; now the static rides on the lane's sound.
    {
        Engine e;
        e.prepare (48000.0, 128);
        juce::AudioBuffer<float> b (2, 128);
        auto peakOver = [&] (double seconds) { float pk = 0.0f; for (int k = 0; k < (int) (seconds * 48000.0 / 128.0); ++k) { e.render (b); pk = std::max (pk, b.getMagnitude (0, 128)); } return pk; };
        auto db = [] (float x) { return juce::Decibels::gainToDecibels (x, -160.0f); };
        const float cold = peakOver (4.0);
        e.setRunning (true);
        const float playing = peakOver (8.0);
        e.setRunning (false);
        peakOver (10.0);                                        // the echoes and the reverb die away, the static with them
        const float soon = peakOver (2.0);
        peakOver (16.0);
        const float after = peakOver (2.0);
        const bool quiet = cold < 1.0e-5f && playing > 0.1f && soon < 3.0e-3f && after < 1.0e-4f;
        std::printf ("  %s: stopped, the rig is silent: %.0f dBFS before anything has played; after it stops the tails die away, the static with them (%.0f dBFS ten seconds on, %.0f dBFS half a minute on; playing peaks at %.1f)\n",
                     quiet ? "ok  " : "FAIL", db (cold), db (soon), db (after), db (playing));
        if (! quiet) ++bad;
    }

    // The kick alone: its first hit lands on the same sample whatever the block size.
    int on[4] = { -1, -1, -1, -1 }; const int blocks[4] = { 64, 100, 512, 1024 };
    for (int k = 0; k < 4; ++k) run (blocks[k], 1, nullptr, 0, nullptr, nullptr, nullptr, &on[k]);
    std::printf ("  kick's first hit at sample %d / %d / %d / %d (blocks of 64, 100, 512, 1024)\n", on[0], on[1], on[2], on[3]);
    for (int k = 1; k < 4; ++k) if (on[k] != on[0] || on[0] < 0) { std::printf ("  FAIL: timing depends on the block size\n"); ++bad; break; }

    // Every lane given one hit on step 1: each lane's first sound should leave together.
    {
        std::printf ("  first sound of each lane, one hit on step 1:");
        int lo = 1 << 30, hi = 0;
        for (int l = 0; l < kLanes; ++l)
        {
            Engine e;
            e.prepare (48000.0, 512);
            Pattern p;
            p.steps[0].on = true; p.steps[0].vel = 1.0f; p.steps[0].gate = 2.0f; p.steps[0].octave = lanes()[(size_t) l].octave;
            for (int k = 0; k < kLanes; ++k) e.setPattern (k, k == l || k == 0 ? p : Pattern());
            for (int k = 0; k < kLanes; ++k) for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (k, sl, -1);     // RADIO hisses on its own
            e.solo = l;
            for (int k = 0; k < cx::kCount; ++k) if (e.control (k).id.endsWith (".attack")) e.set (k, 0.0f);
            e.setRunning (true);
            juce::AudioBuffer<float> b (2, 512);
            int on = -1;
            for (int done = 0; done < 48000 && on < 0; done += 512)
            {
                e.render (b);
                for (int i = 0; i < 512 && on < 0; ++i) if (std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i)) > 2e-3f) on = done + i;
            }
            std::printf (" %d", on);
            if (on >= 0 && l != 6) { lo = std::min (lo, on); hi = std::max (hi, on); }      // RUMBLE is a tail: it swells in late
        }
        std::printf ("\n");
        if (hi - lo > 480) { std::printf ("  FAIL: lanes leave more than 10 ms apart\n"); ++bad; }
    }

    if (wavPath.isNotEmpty())
    {
        juce::File f (wavPath);
        f.deleteFile();
        juce::WavAudioFormat wav;
        if (auto out = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
            if (auto* w = wav.createWriterFor (out.get(), 48000.0, 2, 24, {}, 0))
            {
                out.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                const int frames = (int) audio.size() / 2;
                juce::AudioBuffer<float> b (2, frames);
                for (int i = 0; i < frames; ++i) { b.setSample (0, i, audio[(size_t) i * 2]); b.setSample (1, i, audio[(size_t) i * 2 + 1]); }
                writer->writeFromAudioSampleBuffer (b, 0, frames);
                std::printf ("  wrote %s\n", wavPath.toRawUTF8());
            }
    }
    std::printf (bad == 0 ? "ALL CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", bad);
    return bad;
}



// Every lane knob, turned fully down and fully up against where it starts: how far the lane's sound
// moves (the difference signal against the sound itself, in dB). A knob that moves nothing is listed.
static int knobAudit (int only)
{
    using namespace sf0;
    auto render = [] (int laneIdx, int ctl, float v, bool echoes)
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.solo = laneIdx;
        if (echoes) e.set (cx::lane (laneIdx, 9), 0.6f);          // DENSITY up, so the echo knobs have echoes to shape
        if (ctl >= 0) e.set (ctl, v);
        e.setRunning (true);
        const int total = (int) (4 * 4.0 * 48000.0 * 60.0 / e.tempo());
        std::vector<float> out; out.reserve ((size_t) total);
        juce::AudioBuffer<float> b (2, 512);
        for (int done = 0; done < total; done += 512)
        {
            e.render (b);
            for (int i = 0; i < 512; ++i) out.push_back (0.5f * (b.getSample (0, i) + b.getSample (1, i)));
        }
        return out;
    };
    auto diffDb = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        double d = 0.0, s = 0.0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) { d += (double) (a[i] - b[i]) * (a[i] - b[i]); s += (double) a[i] * a[i]; }
        return 10.0 * std::log10 ((d + 1e-15) / (s + 1e-15));
    };
    std::printf ("SF-000 knob audit (difference against the lane's own sound; above about -35 dB is plain to hear)\n");
    {
        const auto a = render (0, -1, 0, false), b = render (0, -1, 0, false);
        std::printf ("  the same render twice differs by %.1f dB (the engine %s)\n", diffDb (a, b), diffDb (a, b) < -100.0 ? "repeats exactly" : "does not repeat exactly");
    }
    int dead = 0;
    for (int l = 0; l < kLanes; ++l)
    {
        if (only >= 0 && l != only) continue;
        const auto base = render (l, -1, 0, false), baseEcho = render (l, -1, 0, true);
        Engine probe;
        std::printf ("  %-10s", lanes()[(size_t) l].name);
        for (int k = 0; k < kLaneKnobs; ++k)
        {
            const int ctl = cx::lane (l, k);
            const juce::String label = probe.control (ctl).label;
            const bool echo = label.startsWith ("ECHO");
            const auto& ref = echo ? baseEcho : base;
            const double lo = diffDb (ref, render (l, ctl, 0.0f, echo)), hi = diffDb (ref, render (l, ctl, 1.0f, echo));
            const double best = std::max (lo, hi);
            std::printf (" %s %.0f%s", label.replaceCharacter (' ', '_').toRawUTF8(), best, best < -35.0 ? "(!)" : "");
            if (best < -35.0) ++dead;
        }
        std::printf ("\n");
    }
    std::printf ("%d knob(s) make no plain difference with the default patterns\n", dead);
    return 0;
}


// Clicks and drags every control on every screen with the engine running, then checks a few
// results: the screens must survive anything a hand can do to them.
static int uiTest()
{
    using namespace sf0;
    int bad = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++bad; };
    Engine e;
    e.prepare (48000.0, 512);
    Session ses (e, false);
    e.setRunning (true);
    Screen sc (ses);
    sc.setSize ((int) Screen::kW, (int) Screen::kH);
    juce::AudioBuffer<float> b (2, 512);
    auto draw = [&] { sc.createComponentSnapshot (sc.getLocalBounds(), false, 0.25f); };
    auto audio = [&] (int blocks) { for (int i = 0; i < blocks; ++i) e.render (b); };
    auto ev = [&] (juce::Point<float> p, juce::Point<float> down, bool dragged)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &sc, &sc, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
    };
    auto click = [&] (juce::Point<float> p) { sc.mouseDown (ev (p, p, false)); sc.mouseUp (ev (p, p, false)); };
    auto dragBy = [&] (juce::Point<float> p, float dx, float dy)
    {
        sc.mouseDown (ev (p, p, false));
        for (int k = 1; k <= 4; ++k) sc.mouseDrag (ev (p + juce::Point<float> (dx, dy) * ((float) k / 4.0f), p, true));
        sc.mouseUp (ev (p + juce::Point<float> (dx, dy), p, true));
    };
    auto stepKey = [&] (int index) { for (auto& h : sc.testHits()) if (Screen::testIsStep (h) && h.a == index) return h.r.getCentre(); return juce::Point<float>(); };

    std::printf ("SF-000 screen test\n");
    // 1. the sequencer's rules, on the LEAD lane as it starts
    draw();
    click (stepKey (1));
    check (ses.pattern (7).steps[1].on && ses.selStep == 1, "a tap on an unlit step lights and selects it");
    click (stepKey (3));
    check (ses.pattern (7).steps[3].on && ses.selStep == 3, "a tap on a lit step selects it and leaves it lit");
    click (stepKey (3));
    check (! ses.pattern (7).steps[3].on, "a second tap puts it out");
    draw();
    dragBy (stepKey (0), 135.0f, 0.0f);
    check (std::abs (ses.pattern (7).steps[0].gate - 1.25f) < 0.01f, "a gate dragged right stops just past the next lit step: legato into it");
    click (stepKey (1)); click (stepKey (1));     // step 2 out again
    draw();
    dragBy (stepKey (0), 135.0f, 0.0f);
    check (ses.pattern (7).steps[0].gate > 2.5f, "with that step out, the gate runs on as a tie");
    dragBy (stepKey (9), 0.0f, -30.0f);
    check (ses.pattern (7).steps[9].vel > 0.95f, "a step dragged up gets louder");
    draw();
    click (stepKey (6)); click (stepKey (6));     // step 7 held D#3 and a legato gate: put it out
    {
        const auto st = ses.pattern (7).steps[6];
        check (! st.on && st.note == 0 && st.octave == 3 && st.ratchet == 1 && std::abs (st.gate - 1.0f) < 0.001f && std::abs (st.vel - 0.75f) < 0.001f,
               "a step put out forgets its note, gate, velocity and ratchet");
    }
    click (stepKey (6));
    check (ses.pattern (7).steps[6].on && std::abs (ses.pattern (7).steps[6].vel - 0.75f) < 0.001f && std::abs (ses.pattern (7).steps[6].gate - 1.0f) < 0.001f,
           "a new step starts at velocity 75 and a whole step of gate");
    {
        // the strip's M key mutes the sequencer of the lane it shows, and no other
        ses.selectLane (0); draw();
        const int sm = cx::lane (0, cx::kSeqMute);
        juce::Point<float> at; int found = 0;
        for (auto& h : sc.testHits()) { if (Screen::testIsToggle (h, sm)) { at = h.r.getCentre(); ++found; } for (int l = 1; l < kLanes; ++l) if (Screen::testIsToggle (h, cx::lane (l, cx::kSeqMute))) found += 100; }
        audio (200);
        double k0 = e.laneSq[0], h0 = e.laneSq[1];
        audio (200);
        const double kickOn = e.laneSq[0] - k0, hatOn = e.laneSq[1] - h0;
        click (at); audio (400);                    // the kick in the air dies away
        k0 = e.laneSq[0]; h0 = e.laneSq[1];
        audio (200);
        const double kickOff = e.laneSq[0] - k0, hatOff = e.laneSq[1] - h0;
        const bool lit = e.target (sm) > 0.5f;
        const auto shot = ses.take();
        click (at); audio (4);
        const bool back = e.target (sm) < 0.5f;
        ses.recall (shot);                          // taken while it was muted
        audio (4);
        check (found == 1 && lit && back && kickOn > 1e-3 && kickOff < kickOn * 1e-4 && hatOff > hatOn * 0.5,
               "the sequencer's M key stops the kick's steps and leaves the hat's playing; a second press brings them back");
        check (e.target (sm) < 0.5f, "it is not a snapshot's to change: recalling one taken while it was muted leaves it playing");
        ses.selectLane (7); draw();
    }

    // 2. everything, on every page: a click, then a drag on what turns
    int clicks = 0;
    for (auto page : { Screen::pMain, Screen::pLibrary, Screen::pMacros })
    {
        sc.setPage (page); draw();
        const auto all = sc.testHits();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (Screen::testSkips (all[i])) continue;
            sc.setPage (page);
            const auto p = all[i].r.getCentre();
            click (p);
            if (Screen::testIsKnob (all[i]) || Screen::testIsStep (all[i])) { dragBy (p, 0.0f, -60.0f); dragBy (p, 90.0f, 0.0f); dragBy (p, 0.0f, 200.0f); }
            sc.mouseDoubleClick (ev (p, p, false));
            ++clicks;
            if (i % 7 == 0) { draw(); audio (2); }
        }
    }
    // every CONFIG panel: each lane, the lead's two inserts, both returns
    std::vector<Engine::Target> targets;
    for (int l = 0; l < kLanes; ++l) targets.push_back ({ 0, l, 0 });
    targets.push_back ({ 1, 7, 0 }); targets.push_back ({ 1, 7, 1 }); targets.push_back ({ 2, 0, 0 }); targets.push_back ({ 2, 0, 1 });
    int cfgClicks = 0;
    for (auto t : targets)
    {
        sc.setPage (Screen::pMain);
        if (t.kind == 1) ses.activeLane = t.lane;
        sc.showConfig (t); draw();
        const auto all = sc.testHits();
        for (auto& h : all)
        {
            if (! Screen::testIsConfig (h)) continue;
            const auto p = h.r.getCentre();
            click (p); dragBy (p, 0.0f, -45.0f); dragBy (p, 60.0f, 0.0f); dragBy (p, 0.0f, 120.0f);
            sc.mouseDoubleClick (ev (p, p, false));
            ++cfgClicks;
            if (cfgClicks % 5 == 0) { draw(); audio (2); }
        }
        draw(); audio (4);
    }
    check (cfgClicks > 150, (juce::String (cfgClicks) + " CONFIG controls clicked, dragged and double-clicked on 15 panels").toRawUTF8());
    // LOAD: a sound file taken from a list in CONFIG's own panel (no window of the system's, so none of it leaves full screen)
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_load_test");
        tmp.deleteRecursively();
        const auto phrase = tmp.getChildFile ("phrases").getChildFile ("spoken.wav");
        phrase.getParentDirectory().createDirectory();
        tmp.getChildFile ("notes.txt").replaceWithText ("not a sound");
        {
            juce::AudioBuffer<float> w (2, 48000);
            for (int i = 0; i < 48000; ++i) { const float v = 0.4f * (float) std::sin (0.03 * (double) i); w.setSample (0, i, v); w.setSample (1, i, v); }
            juce::WavAudioFormat wav;
            if (auto out = phrase.createOutputStream())
                if (std::unique_ptr<juce::AudioFormatWriter> wr { wav.createWriterFor (out.get(), 48000.0, 2, 16, {}, 0) }) { out.release(); wr->writeFromAudioSampleBuffer (w, 0, 48000); }
        }
        sc.setLoadFolder (false, tmp.getFullPathName());
        sc.setPage (Screen::pMain);
        sc.showConfig ({ 0, 10, 0 }); draw();                           // VOICE
        auto first = [&] (bool (*is) (const Screen::TestHit&)) { for (auto& h : sc.testHits()) if (is (h)) return h.r.getCentre(); return juce::Point<float>(); };
        auto count = [&] (bool (*is) (const Screen::TestHit&)) { int n = 0; for (auto& h : sc.testHits()) if (is (h)) ++n; return n; };
        const bool keyThere = count (Screen::testIsLoadKey) == 1 && e.laneFile (10).isEmpty();
        click (first (Screen::testIsLoadKey)); draw();
        const bool listed = sc.testLoadOpen() && count (Screen::testIsFile) == 1;      // one folder, and the text file is not offered
        click (first (Screen::testIsFile)); draw();     // into the folder
        const bool inside = sc.testLoadOpen() && count (Screen::testIsFile) == 1;
        click (first (Screen::testIsFile)); draw();     // the file
        const bool loaded = ! sc.testLoadOpen() && juce::File (e.laneFile (10)) == phrase && sc.loadFolder (false) == phrase.getParentDirectory().getFullPathName();
        check (keyThere && listed && inside && loaded, "LOAD on a lane that takes a file lists folders and sound files in CONFIG's own panel; a click goes into a folder, a click on a file loads it, and it opens there next time");
        tmp.deleteRecursively();
    }
    audio (200);
    draw();
    bool finite = true;
    for (int i = 0; i < 512; ++i) finite = finite && std::isfinite (b.getSample (0, i)) && std::isfinite (b.getSample (1, i));
    check (true, (juce::String (clicks) + " controls clicked, dragged and double-clicked across three screens").toRawUTF8());
    check (finite && ! e.fault.load(), "the audio is still clean afterwards");

    // 2b. GLIDE set on the screen, then a snapshot knob turned on the screen: the pushed knobs travel
    {
        Engine eg; eg.prepare (48000.0, 512);
        Session sg (eg, false);
        Screen scg (sg);
        scg.setSize ((int) Screen::kW, (int) Screen::kH);
        juce::AudioBuffer<float> bg (2, 512);
        auto drawG = [&] { scg.createComponentSnapshot (scg.getLocalBounds(), false, 0.25f); };
        auto evG = [&] (juce::Point<float> p, juce::Point<float> down, bool dragged)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &scg, &scg, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        auto dragG = [&] (juce::Point<float> p, float dy)
        {
            scg.mouseDown (evG (p, p, false));
            for (int k = 1; k <= 4; ++k) scg.mouseDrag (evG (p + juce::Point<float> (0.0f, dy * (float) k / 4.0f), p, true));
            scg.mouseUp (evG (p + juce::Point<float> (0.0f, dy), p, true));
        };
        for (int running = 0; running < 2; ++running)
        {
            eg.setRunning (running == 1);
            for (int i = 0; i < 30; ++i) eg.render (bg);
            drawG();
            juce::Point<float> glideKnob, energyKnob;
            for (auto& h : scg.testHits()) { if (Screen::testIsControl (h, cx::kGlide)) glideKnob = h.r.getCentre(); if (Screen::testIsSnapKnob (h, 0)) energyKnob = h.r.getCentre(); }
            sg.userSet (cx::kGlide, 0.0f); sg.turnSnapKnob (0, 0.0f);
            sg.userSet (cx::lane (0, 5), 0.3f); sg.userSet (cx::ins (7, 0, 1), 0.3f);
            for (int i = 0; i < 10; ++i) eg.render (bg);
            dragG (glideKnob, -14.0f * 4.0f);                             // four places up: 2 BARS
            const juce::String set = eg.valueText (cx::kGlide);
            const int dens = eg.find ("closed_hat.density");
            const float d0 = eg.shown (dens);
            dragG (energyKnob, -160.0f);                                  // ENERGY all the way up
            for (int i = 0; i < 100; ++i) eg.render (bg);                 // about a second
            const float mid = eg.shown (dens), to = eg.target (dens);
            const bool going = eg.isGliding (dens);
            for (int i = 0; i < 600; ++i) eg.render (bg);                 // past two bars
            check (set == "2 BARS" && to > d0 + 0.2f && going && mid > d0 + 0.02f && mid < to - 0.02f && std::abs (eg.shown (dens) - to) < 1e-4f,
                   (juce::String (running == 1 ? "running" : "stopped") + ": GLIDE at " + set + ", ENERGY turned up on the screen: CLOSED HAT DENSITY is on its way after a second ("
                    + juce::String (d0, 2) + " > " + juce::String (mid, 2) + " > " + juce::String (to, 2) + ") and there after two bars").toRawUTF8());

            // a knob under the hand travels too: the kick's PUNCH, and a knob of the lead's delay
            sg.activeLane = 7;
            drawG();
            const int punch = cx::lane (0, 5), fdbk = cx::ins (7, 0, 1);
            juce::Point<float> punchKnob, fdbkKnob;
            for (auto& h : scg.testHits()) { if (Screen::testIsControl (h, punch)) punchKnob = h.r.getCentre(); if (Screen::testIsControl (h, fdbk)) fdbkKnob = h.r.getCentre(); }
            const float p0 = eg.shown (punch), f0 = eg.shown (fdbk);
            dragG (punchKnob, -60.0f); dragG (fdbkKnob, -60.0f);
            const float pTo = eg.target (punch), fTo = eg.target (fdbk);
            for (int i = 0; i < 100; ++i) eg.render (bg);
            const float pMid = eg.shown (punch), fMid = eg.shown (fdbk);
            const bool both = eg.isGliding (punch) && eg.isGliding (fdbk);
            dragG (punchKnob, -20.0f);                                    // turned again on the way: on from where the sound is
            eg.render (bg);
            const float pNext = eg.shown (punch), pTo2 = eg.target (punch);
            for (int i = 0; i < 600; ++i) eg.render (bg);
            check (both && pTo > p0 + 0.2f && pMid > p0 + 0.02f && pMid < pTo - 0.02f && fMid > f0 + 0.02f && fMid < fTo - 0.02f
                   && std::abs (pNext - pMid) < 0.02f && pTo2 > pTo && std::abs (eg.shown (punch) - pTo2) < 1e-4f && std::abs (eg.shown (fdbk) - fTo) < 1e-4f,
                   (juce::String (running == 1 ? "running" : "stopped") + ": a knob turned by hand travels there over the GLIDE time (KICK PUNCH " + juce::String (p0, 2) + " > "
                    + juce::String (pMid, 2) + " > " + juce::String (pTo, 2) + "; the delay's FEEDBACK too), and turned again on the way it carries on without a jump").toRawUTF8());
            sg.userSet (cx::kGlide, 0.0f);
            dragG (punchKnob, 40.0f);
            eg.render (bg);
            check (std::abs (eg.shown (punch) - eg.target (punch)) < 1e-6f && ! eg.isGliding (punch), "with GLIDE off it is there at once");
        }
    }

    // 2b'. INFO: what MIDI reaches is numbered, and what is pointed at says what it is
    {
        Engine ei; ei.prepare (48000.0, 512);
        Session si (ei, false);
        Screen sci (si);
        sci.setSize ((int) Screen::kW, (int) Screen::kH);
        juce::Image img (juce::Image::RGB, (int) Screen::kW, (int) Screen::kH, true);
        auto whole = [&] { juce::Graphics g (img); sci.paintEntireComponent (g, true); };
        whole();
        int lines = 0, missing = 0;
        for (auto& ln : lanes()) for (auto& k : ln.knobs) { ++lines; if (help::param (ln.module, k.param) == nullptr) ++missing; }
        for (auto& fx : effects()) for (auto& c : fx.cells) if (c.param != nullptr) { ++lines; if (help::param (fx.module, c.param) == nullptr) ++missing; }
        for (auto* fx : { &returnA(), &returnB (0), &returnB (1) }) for (auto& c : fx->cells) if (c.param != nullptr) { ++lines; if (help::param (fx->module, c.param) == nullptr) ++missing; }
        // CONFIG's controls too: everything an instrument, an effect or a return has that is not on its panel
        int cfgLines = 0, cfgMissing = 0;
        {
            Engine ec; ec.prepare (48000.0, 128);
            auto all = [&] (Engine::Target t, const char* module)
            {
                auto* p = ec.processorFor (t);
                if (p == nullptr) return;
                juce::StringArray ids;
                for (int i = 0; i < p->numModuleParams(); ++i) if (p->allSpecs()[(size_t) i].id != "alloc" && ! p->allSpecs()[(size_t) i].hidden) ids.add (p->allSpecs()[(size_t) i].id);
                for (auto& sp : ec.setupParams (t)) ids.addIfNotAlreadyThere (sp.id);
                for (auto& id : ids)
                {
                    ++cfgLines;
                    if (help::param (module, id.toRawUTF8()) == nullptr) { ++cfgMissing; std::printf ("    no line: %s | %s | %s\n", module, id.toRawUTF8(), p->allSpecs()[(size_t) std::max (0, p->paramIndex (id.toStdString()))].name.c_str()); }
                }
            };
            for (int l = 0; l < kLanes; ++l) all ({ 0, l, 0 }, lanes()[(size_t) l].module);
            for (int ty = 0; ty < (int) effects().size(); ++ty) { ec.setInsertType (0, 0, ty); all ({ 1, 0, 0 }, effects()[(size_t) ty].module); }
            all ({ 2, 0, 0 }, returnA().module);
            for (int m = 0; m < kSpaceModels; ++m) { ec.setSpaceModel (m); all ({ 2, 0, 1 }, returnB (m).module); }
        }

        check (missing == 0 && lines > 180, (juce::String (lines) + " knobs and places (every lane's twelve, every effect's, the returns') each have a line saying what they do").toRawUTF8());
        check (cfgMissing == 0 && cfgLines > 150, (juce::String (cfgLines) + " CONFIG controls (every instrument's, effect's and return's, MOTION's too) each have a line as well").toRawUTF8());

        check (missing == 0 && lines > 180, (juce::String (lines) + " knobs and places (every lane's twelve, every effect's, the returns') each have a line saying what they do").toRawUTF8());

        sci.testPointAt ({ 60.0f, 134.0f });
        const bool quiet = sci.testTip().isEmpty();
        const bool noKeysYet = ! sci.testKeysShown();
        sci.keyPressed (juce::KeyPress ('i', juce::ModifierKeys(), 'i'));
        whole();
        {
            // INFO also lists the keyboard's keys, at the foot of the screen; it steps to the other side when pointed at
            const auto left = sci.testKeysBox();
            const bool shown = sci.testKeysShown() && left.getX() < 100.0f && left.getBottom() > Screen::kH - 40.0f;
            const auto in = left.getCentre();
            sci.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), in, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             &sci, &sci, juce::Time::getCurrentTime(), in, juce::Time::getCurrentTime(), 1, false));
            whole();
            const bool aside = sci.testKeysShown() && sci.testKeysBox().getX() > Screen::kW * 0.5f;
            const juce::Point<float> away (700.0f, 300.0f);
            sci.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), away, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             &sci, &sci, juce::Time::getCurrentTime(), away, juce::Time::getCurrentTime(), 1, false));
            whole();
            check (noKeysYet && shown && aside && sci.testKeysBox().getX() < 100.0f, "INFO lists the keyboard's keys at the foot of the screen, and the list steps to the other side while it is pointed at");
        }
        int asked = 0, silent = 0, onMidi = 0;
        for (auto& h : sci.testHits())
        {
            if (! Screen::testAnswers (h)) continue;
            sci.testPointAt (h.r.getCentre());
            const auto tip = sci.testTip();
            ++asked;
            if (tip.trim().isEmpty()) ++silent;
            if (tip.contains ("MIDI: CHANNEL")) ++onMidi;
        }
        check (sci.isInfo() && quiet && asked > 250 && silent == 0 && onMidi > 180,
               ("I on the keyboard turns INFO on: off, pointing says nothing; on, each of the " + juce::String (asked) + " knobs, keys and names on the main screen says what it is, "
                   + juce::String (onMidi) + " of them with a MIDI channel and number").toRawUTF8());

        // with a box showing, a part painted on its own is still the picture a whole paint gives
        juce::Rectangle<float> aKnob;
        for (auto& h : sci.testHits()) if (Screen::testIsControl (h, cx::lane (6, 8))) aKnob = h.r;
        sci.testPointAt (aKnob.getCentre());
        const auto box = sci.testTipRect();
        whole();
        const auto all = img.createCopy();
        for (auto r : { box, aKnob.getSmallestIntegerContainer().expanded (14, 6), box.withTrimmedLeft (box.getWidth() / 2), juce::Rectangle<int> (1151, 782, 554, 190) })
        {
            juce::Graphics g (img);
            g.reduceClipRegion (r);
            sci.paintEntireComponent (g, true);
        }
        int rowsOff = 0;
        {
            const juce::Image::BitmapData x (all, juce::Image::BitmapData::readOnly), y (img, juce::Image::BitmapData::readOnly);
            for (int row = 0; row < all.getHeight(); ++row)
            {
                const auto* p = x.getLinePointer (row); const auto* q = y.getLinePointer (row);
                bool off = false;
                for (int k = 0; k < all.getWidth() * x.pixelStride && ! off; ++k) off = std::abs ((int) p[k] - (int) q[k]) > 6;
                if (off) ++rowsOff;
            }
        }
        check (! box.isEmpty() && sci.testTip().contains ("RUMBLE") && sci.testTip().contains ("CC 20") && rowsOff == 0,
               "pointed at RUMBLE's GATE, the box says so and gives channel 7, CC 20; parts of the screen painted on their own (the box, the knob under it, the meters) leave the picture as a whole paint gives it");

        sci.keyPressed (juce::KeyPress ('h', juce::ModifierKeys(), 'h'));
        const bool phonesOn = ei.phones();
        sci.keyPressed (juce::KeyPress ('h', juce::ModifierKeys(), 'h'));
        check (phonesOn && ! ei.phones(), "H on the keyboard lights PHONES and puts it out");
    }

    // 2c. a part of the screen drawn on its own leaves the picture exactly as drawing all of it would:
    // with the lit skin (whose pictures reach past their own edges: shade, glow) and with the flat drawing
    for (int look = 0; look < 3; ++look)
    {
        Engine ep; ep.prepare (48000.0, 512);
        Session sp (ep, false);
        Screen scp (sp);
        scp.setSize ((int) Screen::kW, (int) Screen::kH);
        int told = 0;
        scp.onLook = [&told] { ++told; };
        if (look == 0) check (scp.isLit() && ! scp.isDark(), "the screen starts with the lit skin, the light instrument");
        else if (look == 1)
        {
            scp.keyPressed (juce::KeyPress ('d', juce::ModifierKeys(), 'd'));
            check (scp.isLit() && scp.isDark() && told == 1, "D on the keyboard gives the dark instrument, and says so to whoever keeps the look");
        }
        else
        {
            scp.keyPressed (juce::KeyPress ('l', juce::ModifierKeys(), 'l'));
            check (! scp.isLit(), "L on the keyboard gives the flat drawing back");
            scp.keyPressed (juce::KeyPress ('d', juce::ModifierKeys(), 'd'));
            check (scp.isLit() && scp.isDark(), "from the flat drawing, D goes to the dark instrument");
            scp.keyPressed (juce::KeyPress ('d', juce::ModifierKeys(), 'd'));
            scp.keyPressed (juce::KeyPress ('l', juce::ModifierKeys(), 'l'));
            check (! scp.isLit() && ! scp.isDark(), "and D then L are back at the flat drawing");
        }
        juce::AudioBuffer<float> bp (2, 512);
        auto whole = [&] (juce::Image& img) { juce::Graphics g (img); scp.paintEntireComponent (g, true); };
        auto part = [&] (juce::Image& img, juce::Rectangle<int> clip) { juce::Graphics g (img); g.reduceClipRegion (clip); scp.paintEntireComponent (g, true); };
        auto same = [] (const juce::Image& a, const juce::Image& b)
        {
            // rows with a pixel that is plainly another colour (the software renderer shades a path's
            // edge by a couple of levels where a clip cuts it; a missing knob or a hole is far more)
            const juce::Image::BitmapData x (a, juce::Image::BitmapData::readOnly), y (b, juce::Image::BitmapData::readOnly);
            int diff = 0;
            for (int row = 0; row < a.getHeight(); ++row)
            {
                const auto* p = x.getLinePointer (row); const auto* q = y.getLinePointer (row);
                bool off = false;
                for (int k = 0; k < a.getWidth() * x.pixelStride && ! off; ++k)
                {
                    off = std::abs ((int) p[k] - (int) q[k]) > 6;
                    if (off && diff < 4) std::printf ("    (row %d, across %d: %d against %d)\n", row, k / x.pixelStride, (int) p[k], (int) q[k]);
                }
                if (off) ++diff;
            }
            return diff;
        };
        bool allSame = true, hitsKept = true;
        int parts = 0;
        for (int cfg = 0; cfg < 3; ++cfg)
        {
            if (cfg == 1) scp.showConfig ({ 0, 0, 0 });               // CONFIG open on the kick (the rows moved over)
            if (cfg == 2) scp.showConfig ({ 0, 10, 0 });              // and on VOICE (the columns gone round)
            ep.setRunning (cfg != 0);
            for (int i = 0; i < 40; ++i) ep.render (bp);
            juce::Image shown (juce::Image::RGB, (int) Screen::kW, (int) Screen::kH, true), fresh (juce::Image::RGB, (int) Screen::kW, (int) Screen::kH, true);
            whole (shown);
            const auto hitsBefore = scp.testHits();
            // things move: three knobs, the meters, the playing step
            const int moved[] = { cx::lane (3, 2), cx::lane (9, 10), cx::lane (cfg == 2 ? 10 : 0, 5) };
            for (int ctl : moved) sp.userSet (ctl, sp.value (ctl) > 0.5f ? 0.2f : 0.8f);
            for (int i = 0; i < 25; ++i) ep.render (bp);
            std::vector<juce::Rectangle<int>> rects;
            for (auto& h : hitsBefore) for (int ctl : moved) if (Screen::testIsControl (h, ctl)) rects.push_back (h.r.expanded (14, 6).getSmallestIntegerContainer());
            for (auto r : scp.testMovingRects()) rects.push_back (r);
            for (auto r : rects) { part (shown, r); ++parts; }
            hitsKept = hitsKept && scp.testHits().size() == hitsBefore.size();
            whole (fresh);
            const int rowsOff = same (shown, fresh);
            allSame = allSame && rowsOff == 0;
            if (rowsOff != 0) std::printf ("    (%d rows differ with CONFIG %d)\n", rowsOff, cfg);
        }
        check (allSame && hitsKept && parts >= 12, (juce::String (look == 0 ? "lit: " : look == 1 ? "dark: " : "flat: ") + juce::String (parts) + " parts of the screen drawn on their own (knobs, the meters, the steps), with CONFIG shut and open: the same picture a whole redraw gives").toRawUTF8());
    }

    // 2d. the REC key: a press starts a take, a short press leaves it running, a hold ends it
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_rec_ui_test");
        tmp.deleteRecursively();
        Recorder rec (e);
        rec.root = tmp;
        sc.recorder = &rec;
        sc.setPage (Screen::pMain); draw();
        juce::Point<float> at; int found = 0;
        for (auto& h : sc.testHits()) if (Screen::testIsRec (h)) { at = h.r.getCentre(); ++found; }
        click (at); audio (20);
        const bool began = rec.recording();
        sc.mouseDown (ev (at, at, false)); sc.testTick(); sc.mouseUp (ev (at, at, false));       // a tap: not long enough
        const bool still = rec.recording();
        audio (20); draw();
        sc.mouseDown (ev (at, at, false)); juce::Thread::sleep (800); sc.testTick();
        const bool ended = ! rec.recording();
        sc.mouseUp (ev (at, at, false));
        draw();
        const bool kept = rec.folder().getChildFile ("00 SET.wav").existsAsFile() && rec.folder().getChildFile ("take.txt").existsAsFile();
        check (found == 1 && began && still && ended && kept, "REC: a press starts a take, a tap leaves it running, a hold ends it and the take is kept");
        sc.recorder = nullptr;
        tmp.deleteRecursively();
    }

    // 2e. ONLINE: the key lights and goes out, says what it needs, and SET UP asks for the panel
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_online_ui_test");
        tmp.deleteRecursively();
        const auto was = Session::stateFileForTests;
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        {
            Online on (ses);
            sc.online = &on;
            bool asked = false;
            sc.onOnlineSetup = [&asked] { asked = true; };
            sc.setPage (Screen::pLibrary); draw();
            juce::Point<float> key, setUp; int found = 0;
            for (auto& h : sc.testHits()) { if (Screen::testIsOnline (h, false)) { key = h.r.getCentre(); ++found; } if (Screen::testIsOnline (h, true)) { setUp = h.r.getCentre(); ++found; } }
            const bool offFirst = ! on.on() && on.status().state == Online::State::off;
            click (key); draw();
            const bool lit = on.on() && on.status().state == Online::State::notSetUp && on.line().contains ("SET UP");
            const bool kept = (bool) juce::JSON::parse (Online::setupFile()).getProperty ("on", false);
            click (setUp);
            click (key); draw();
            const bool out = ! on.on() && on.status().state == Online::State::off;
            check (found == 2 && offFirst && lit && kept && asked && out, "ONLINE: the key lights and goes out and is remembered, says it needs setting up, and SET UP asks for its panel");
            sc.online = nullptr; sc.onOnlineSetup = nullptr;
            sc.setPage (Screen::pMain); draw();
        }
        Session::stateFileForTests = was;
        tmp.deleteRecursively();
    }

    // 3. snapshots and the knobs, on a fresh rig
    {
        Engine e2; e2.prepare (48000.0, 512);
        Session s2 (e2, false);
        const int dens = e2.find ("closed_hat.density"), res = e2.find ("lead.res");
        const size_t before = s2.library.size();
        s2.userSet (res, 0.2f);
        const auto mine = s2.take();
        check (s2.library.size() == before + 1, "SNAPSHOT adds one to the library");
        s2.turnSnapKnob (0, 1.0f);
        check (std::abs (e2.target (dens) - 0.45f) < 0.01f, "ENERGY turned up moves CLOSED HAT DENSITY to its snapshot");
        s2.turnSnapKnob (2, 1.0f);
        check (e2.target (res) > 0.3f, "ACID stacks on top, from the hand-set RES");
        s2.turnSnapKnob (0, 0.0f); s2.turnSnapKnob (2, 0.0f);
        check (std::abs (e2.target (dens)) < 0.01f && std::abs (e2.target (res) - 0.2f) < 0.01f, "knobs back at zero leave the base scene as it was");
        s2.userSet (res, 0.9f);
        s2.pressGenerator (0, 1);
        int hits = 0; for (int i = 0; i < 16; ++i) hits += s2.pattern (0).steps[(size_t) i].on ? 1 : 0;
        check (hits == 5 && s2.genType[0] == 1, "EUCLID writes five hits on the kick and stays armed");
        s2.recall (mine);
        hits = 0; for (int i = 0; i < 16; ++i) hits += s2.pattern (0).steps[(size_t) i].on ? 1 : 0;
        check (std::abs (e2.target (res) - 0.2f) < 0.01f && hits == 4, "recalling the snapshot brings back its knobs and its pattern");
        e2.setRunning (true);
        juce::AudioBuffer<float> b2 (2, 512);
        s2.stepInsert (0, 0); s2.stepInsert (0, 0); s2.stepInsert (0, 1);
        for (int i = 0; i < 50; ++i) e2.render (b2);
        const auto withFx = s2.take();
        s2.recall (1000);
        for (int i = 0; i < 50; ++i) e2.render (b2);
        check (e2.insertType (0, 0) == -1, "recalling INIT takes the kick's inserts out");
        s2.recall (withFx);
        for (int i = 0; i < 50; ++i) e2.render (b2);
        check (e2.insertType (0, 0) == 1 && e2.insertType (0, 1) == 0 && ! e2.fault.load(), "recalling the other puts them back, while it plays");
    }
    // 4. mute and solo, CLEAN SLATE, setup in snapshots, a phrase dropped on VOICE
    {
        Engine e3; e3.prepare (48000.0, 512);
        Session s3 (e3, false);
        e3.setRunning (true);
        juce::AudioBuffer<float> b3 (2, 512);
        auto run = [&] (int blocks) { for (int i = 0; i < blocks; ++i) e3.render (b3); };
        run (40);
        s3.userSet (cx::lane (0, cx::kSolo), 1.0f);
        run (4);
        double hat = e3.laneSq[1], kick = e3.laneSq[0];
        run (200);
        check (e3.laneSq[1] - hat < 1e-9 && e3.laneSq[0] - kick > 1e-3, "SOLO on the kick silences the other lanes");
        s3.userSet (cx::lane (0, cx::kSolo), 0.0f);
        s3.userSet (cx::lane (0, cx::kMute), 1.0f);
        run (4);
        hat = e3.laneSq[1]; kick = e3.laneSq[0];
        run (200);
        check (e3.laneSq[0] - kick < 1e-9 && e3.laneSq[1] - hat > 1e-6, "MUTE on the kick silences only the kick");
        s3.userSet (cx::lane (0, cx::kMute), 0.0f);
        s3.userSet (cx::insMute (7, 0), 1.0f); s3.userSet (cx::insSolo (7, 1), 1.0f); s3.userSet (cx::kRetMS + 1, 1.0f);
        run (100);
        s3.userSet (cx::insMute (7, 0), 0.0f); s3.userSet (cx::insSolo (7, 1), 0.0f); s3.userSet (cx::kRetMS + 1, 0.0f);
        run (100);
        check (! e3.fault.load(), "insert and return MUTE and SOLO switch cleanly while it plays");

        const Engine::Target kickT { 0, 0, 0 };
        auto model = [&] { for (auto& kv : e3.getSetup (kickT)) if (kv.first == "model") return kv.second; return -1.0f; };
        const float m0 = model();
        e3.setSetup (kickT, "model", 1.0f);
        const auto withModel = s3.take();
        e3.setSetup (kickT, "model", m0);
        s3.recall (withModel);
        check (std::abs (model() - 1.0f) < 1e-4f && m0 >= 0.0f, "a snapshot keeps the model chosen in CONFIG");
        run (50);

        s3.recall (kCleanSlateId);
        run (50);
        int lit = 0, ins = 0;
        for (int l = 0; l < kLanes; ++l)
        {
            for (auto& st : s3.pattern (l).steps) lit += st.on ? 1 : 0;
            for (int k = 0; k < kSlots; ++k) ins += e3.insertType (l, k) >= 0 ? 1 : 0;
        }
        check (lit == 0 && ins == 0 && std::abs (model() - m0) < 1e-4f, "CLEAN SLATE: no steps, no inserts, the starting models");
    }
    {
        // two seconds of a rising tone, written to disk and dropped on VOICE
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_phrase_test.wav");
        f.deleteFile();
        {
            juce::AudioBuffer<float> tone (1, 96000);
            double ph = 0.0;
            for (int i = 0; i < 96000; ++i) { ph += juce::MathConstants<double>::twoPi * (200.0 + 600.0 * i / 96000.0) / 48000.0; tone.setSample (0, i, 0.5f * (float) std::sin (ph)); }
            juce::WavAudioFormat wav;
            if (auto out = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                if (auto* w = wav.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0)) { out.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (tone, 0, 96000); }
        }
        auto voice = [&] (bool withFile)
        {
            Engine e4; e4.prepare (48000.0, 512);
            bool ok = true;
            if (withFile) ok = e4.loadFile (10, f);
            e4.solo = 10;
            e4.setRunning (true);
            std::vector<float> out;
            juce::AudioBuffer<float> b4 (2, 512);
            for (int i = 0; i < 400; ++i) { e4.render (b4); for (int k = 0; k < 512; ++k) out.push_back (b4.getSample (0, k)); }
            if (! ok) out.clear();
            return out;
        };
        const auto a = voice (false), bfile = voice (true);
        double d = 0.0, q = 0.0;
        for (size_t i = 0; i < a.size() && i < bfile.size(); ++i) { d += (double) (a[i] - bfile[i]) * (a[i] - bfile[i]); q += (double) a[i] * a[i]; }
        check (! bfile.empty() && 10.0 * std::log10 ((d + 1e-15) / (q + 1e-15)) > -20.0, "a phrase dropped on VOICE loads and changes what the lane plays");
        f.deleteFile();
    }
    {
        // 6. SPACE as a convolution: an impulse response (a second of noise dying away) written to disk
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto ir = tmp.getChildFile ("sf000_ir_test.wav"), ir2 = tmp.getChildFile ("sf000_ir_test_b.wav");
        auto writeIr = [] (const juce::File& f, double rate)
        {
            f.deleteFile();
            juce::AudioBuffer<float> x (2, 48000);
            juce::Random rng (7);
            for (int i = 0; i < 48000; ++i)
                for (int ch = 0; ch < 2; ++ch) x.setSample (ch, i, (i == 0 ? 0.9f : 0.5f * (rng.nextFloat() * 2.0f - 1.0f)) * (float) std::exp (-rate * i / 48000.0));
            juce::WavAudioFormat wav;
            if (auto out = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                if (auto* w = wav.createWriterFor (out.get(), 48000.0, 2, 24, {}, 0)) { out.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (x, 0, 48000); }
        };
        writeIr (ir, 5.0); writeIr (ir2, 12.0);
        const Engine::Target space { 2, 0, 1 };
        juce::AudioBuffer<float> b5 (2, 512);
        // the places a convolution holds, read from its own state (slot numbers), once its imports are done
        auto places = [&] (Engine& en)
        {
            std::vector<int> slots;
            for (int tries = 0; tries < 400; ++tries)
            {
                for (int i = 0; i < 8; ++i) en.render (b5);
                juce::Thread::sleep (15);
                if (! en.processorFor (space)->mod().statusLine().startsWith ("IMPORTING") && tries > 6) break;
            }
            juce::ValueTree t ("STATE");
            en.processorFor (space)->mod().save (t);
            for (int i = 0; i < t.getNumChildren(); ++i) if (t.getChild (i).hasType ("PLACE")) slots.push_back ((int) t.getChild (i).getProperty ("slot", -1));
            return slots;
        };
        auto heard = [&] (Engine& en)
        {
            // B alone, two bars
            en.set (en.find ("ret.space_solo"), 1.0f);
            en.setRunning (true);
            std::vector<float> out;
            for (int i = 0; i < 340; ++i) { en.render (b5); if (i >= 40) for (int k = 0; k < 512; ++k) out.push_back (b5.getSample (0, k)); }
            return out;
        };
        auto level = [] (const std::vector<float>& v) { double q = 0.0; for (auto x : v) q += (double) x * x; return 10.0 * std::log10 (q / (double) std::max<size_t> (1, v.size()) + 1e-15); };

        Engine e5; e5.prepare (48000.0, 512);
        check (e5.acceptsFiles (space) && ! e5.acceptsFiles ({ 2, 0, 0 }) && e5.spaceModel() == 0, "SPACE starts as the REVERB and takes a file; DUB takes none");
        const bool loaded = e5.loadFiles (space, juce::StringArray (ir.getFullPathName()));
        check (loaded && e5.spaceModel() == 1 && e5.heldFiles (space).size() == 1, "an impulse response given to SPACE makes it the CONVOLUTION");
        auto got = places (e5);
        check (got.size() == 1 && got[0] == 0, ("the convolution holds it as place 1 (" + e5.processorFor (space)->mod().statusLine() + ")").toRawUTF8());
        e5.setSetup (space, "place", 2.0f / 7.0f);
        e5.loadFiles (space, juce::StringArray (ir2.getFullPathName()));
        got = places (e5);
        check (got.size() == 2 && got[1] == 2 && e5.heldFiles (space).size() == 2, "with PLACE on 3, the next file fills place 3 and leaves place 1");
        const auto conv = heard (e5);
        Engine e6; e6.prepare (48000.0, 512);
        const auto rev = heard (e6);
        check (level (conv) > -70.0 && level (rev) > -70.0 && ! e5.fault.load(), ("both reverbs sound on the B return (convolution " + juce::String (level (conv), 1) + " dB, reverb " + juce::String (level (rev), 1) + " dB)").toRawUTF8());
        // the change of reverb while it plays: no fault, nothing but finite samples, and back again
        bool clean = true;
        for (int k = 0; k < 6; ++k)
        {
            e5.setSpaceModel (k % 2 == 0 ? 0 : 1);
            for (int i = 0; i < 30; ++i) { e5.render (b5); for (int n = 0; n < 512; ++n) clean = clean && std::isfinite (b5.getSample (0, n)) && std::abs (b5.getSample (0, n)) < 4.0f; }
        }
        check (clean && ! e5.fault.load() && e5.spaceModel() == 1, "SPACE changes reverb and back while it plays");

        // a snapshot leaves the reverb SPACE is alone; the session file brings it and its places back
        tmp.getChildFile ("sf000_ui_test").deleteRecursively();
        Session::stateFileForTests = tmp.getChildFile ("sf000_ui_test").getChildFile ("session.json");
        Session::stateFileForTests.deleteFile();
        {
            Engine e7; e7.prepare (48000.0, 512);
            Session s7 (e7, true);
            const auto init = s7.library.size() > 1 ? s7.library[1].id : 0;
            e7.loadFiles (space, juce::StringArray (ir.getFullPathName()));
            e7.setSetup (space, "place", 2.0f / 7.0f);
            e7.loadFiles (space, juce::StringArray (ir2.getFullPathName()));
            s7.recall (init);
            check (e7.spaceModel() == 1, "recalling a snapshot taken on the REVERB leaves SPACE the CONVOLUTION");
            s7.recall (kCleanSlateId);
            check (e7.spaceModel() == 1, "so does CLEAN SLATE");
            s7.touch(); s7.save();
        }
        {
            // as the app does it: the session is read before the audio device starts
            Engine e8;
            Session s8 (e8, true);
            e8.prepare (48000.0, 512);
            const auto back = places (e8);
            check (e8.spaceModel() == 1 && back.size() == 2 && back[0] == 0 && back[1] == 2, "the app opening again finds SPACE a CONVOLUTION with both places");
        }
        Session::stateFileForTests.deleteFile();
        Session::stateFileForTests = juce::File();

        // dropped on a CONFIG panel, a file goes to what that panel is set to
        Engine e9; e9.prepare (48000.0, 512);
        Session s9 (e9, false);
        Screen sc9 (s9);
        sc9.setSize ((int) Screen::kW, (int) Screen::kH);
        auto draw9 = [&] { sc9.createComponentSnapshot (sc9.getLocalBounds(), false, 0.25f); };
        sc9.showConfig (space); draw9();
        auto at = sc9.testConfigRect().getCentre();
        sc9.filesDropped (juce::StringArray (ir.getFullPathName()), (int) at.x, (int) at.y);
        check (e9.spaceModel() == 1 && e9.heldFiles (space).size() == 1 && e9.laneFile (s9.activeLane).isEmpty(), "a file dropped on SPACE's CONFIG goes to SPACE, not to the active lane");
        sc9.showConfig ({ 2, 0, 0 }); draw9();
        at = sc9.testConfigRect().getCentre();
        sc9.filesDropped (juce::StringArray (ir.getFullPathName()), (int) at.x, (int) at.y);
        check (e9.laneFile (s9.activeLane).isEmpty() && e9.heldFiles (space).size() == 1, "dropped on DUB's CONFIG it goes nowhere: the delay takes no file");
        e9.setInsertType (7, 0, 2);       // CONVOLUTION as the lead's first insert
        s9.activeLane = 7;
        sc9.showConfig ({ 1, 7, 0 }); draw9();
        at = sc9.testConfigRect().getCentre();
        sc9.filesDropped (juce::StringArray (ir2.getFullPathName()), (int) at.x, (int) at.y);
        check (e9.heldFiles ({ 1, 7, 0 }).size() == 1 && e9.laneFile (7).isEmpty(), "dropped on a convolution insert's CONFIG it goes to that insert");
        e9.setInsertType (7, 0, 0); e9.setInsertType (7, 0, 2);
        check (e9.heldFiles ({ 1, 7, 0 }).size() == 1, "the insert's file is still held when the effect comes back to the slot");
        sc9.showConfig ({ 0, 10, 0 }); draw9();
        at = sc9.testConfigRect().getCentre();
        sc9.filesDropped (juce::StringArray (ir.getFullPathName()), (int) at.x, (int) at.y);
        check (e9.laneFile (10).isNotEmpty(), "dropped on VOICE's CONFIG it goes to VOICE");

        // CONFIG open: the panel it is for stands right beside it
        std::vector<Engine::Target> all;
        for (int l = 0; l < kLanes; ++l) all.push_back ({ 0, l, 0 });
        for (int k = 0; k < kSlots; ++k) all.push_back ({ 1, 7, k });
        all.push_back ({ 2, 0, 0 }); all.push_back ({ 2, 0, 1 });
        int beside = 0;
        for (auto t : all)
        {
            if (t.kind == 1) s9.activeLane = t.lane;
            sc9.showConfig (t); draw9();
            const float edge = sc9.testConfigRect().getRight();
            for (auto& h : sc9.testHits()) if (Screen::testIsPanelOf (h, t) && h.r.getX() >= edge && h.r.getX() - edge < 12.0f && h.r.getRight() <= Screen::kW) { ++beside; break; }
        }
        check (beside == (int) all.size(), (juce::String (beside) + " of " + juce::String ((int) all.size()) + " CONFIG panels open with their performance panel beside them").toRawUTF8());

        // 7. an insert travels with snapshots whole: the effect, its CONFIG, the files it holds
        const Engine::Target leadIns { 1, 7, 0 };
        auto placeNames = [&] (Engine& en, Engine::Target t, bool wait)
        {
            juce::StringArray names;
            auto* pr = en.processorFor (t);
            if (pr == nullptr) return names;
            for (int tries = 0; wait && tries < 400; ++tries)
            {
                for (int i = 0; i < 8; ++i) en.render (b5);
                juce::Thread::sleep (15);
                if (! pr->mod().statusLine().startsWith ("IMPORTING") && tries > 6) break;
            }
            juce::ValueTree st ("STATE");
            pr->mod().save (st);
            for (int i = 0; i < st.getNumChildren(); ++i) if (st.getChild (i).hasType ("PLACE")) names.add (st.getChild (i).getProperty ("slot").toString() + ":" + st.getChild (i).getProperty ("name").toString());
            return names;
        };
        auto blocks = [&] (Engine& en, int nb) { for (int i = 0; i < nb; ++i) en.render (b5); };
        Session::stateFileForTests = tmp.getChildFile ("sf000_ui_test").getChildFile ("session2.json");
        Session::stateFileForTests.deleteFile();
        juce::int64 snapA = 0, snapB = 0, snapInit = 0;
        {
            Engine e10; e10.prepare (48000.0, 512);
            Session s10 (e10, true);
            e10.setRunning (true);
            snapInit = s10.take();                                   // the lead's first insert is the delay
            e10.setInsertType (7, 0, 2);                             // now a convolution, with a place
            e10.loadFiles (leadIns, juce::StringArray (ir.getFullPathName()));
            { const auto got1 = placeNames (e10, leadIns, true).joinIntoString (",");
              check (got1.equalsIgnoreCase ("0:sf000_ir_test"), "a convolution insert on the lead takes an impulse response"); }
            const int lengthParam = 3;                               // a CONFIG choice of the insert's, to see it come back
            e10.setSetup (leadIns, "length", (float) lengthParam / 7.0f);
            snapA = s10.take();
            const auto sa = s10.findSnap (snapA);
            check (sa != nullptr && sa->hasInsFiles && sa->insFiles[7][0].size() == 1 && sa->inserts[7][0] == 2, "the snapshot holds the insert and its file");

            s10.recall (snapInit); blocks (e10, 6);
            check (e10.insertType (7, 0) == 0 && e10.insertFiles (7, 0).empty(), "recalling the earlier snapshot puts the delay back");
            ir.moveFileTo (tmp.getChildFile ("sf000_ir_moved.wav"));  // the disk can no longer help
            s10.recall (snapA);
            auto length = [&] { for (auto& sp : e10.setupParams (leadIns)) if (sp.id == "length") return (int) std::lround (sp.value * 7.0f); return -1; };
            check (e10.insertType (7, 0) == 2 && placeNames (e10, leadIns, false).joinIntoString (",").equalsIgnoreCase ("0:sf000_ir_test") && length() == lengthParam,
                   "recalling it brings the convolution back with its place and its CONFIG at once, without the disk");
            tmp.getChildFile ("sf000_ir_moved.wav").moveFileTo (ir);
            blocks (e10, 6);

            // a second snapshot with another place on the same insert: each recall brings its own
            e10.loadFiles (leadIns, juce::StringArray (ir2.getFullPathName()));
            placeNames (e10, leadIns, true);
            snapB = s10.take();
            s10.recall (snapA);
            check (placeNames (e10, leadIns, true).joinIntoString (",").equalsIgnoreCase ("0:sf000_ir_test"), "the first snapshot still brings back the first place (read again from the disk this once)");
            blocks (e10, 6);
            s10.recall (snapB);
            check (placeNames (e10, leadIns, false).joinIntoString (",").equalsIgnoreCase ("0:sf000_ir_test_b"), "and the second its own, at once");
            blocks (e10, 6);
            s10.recall (snapA);
            check (placeNames (e10, leadIns, false).joinIntoString (",").equalsIgnoreCase ("0:sf000_ir_test") && ! e10.fault.load(), "back and forth, each at once, while it plays");
            s10.recall (snapInit);
            s10.touch(); s10.save();
        }
        {
            // the app opening again on the delay: the snapshots' convolutions are read ahead
            Engine e11;
            Session s11 (e11, true);
            e11.prepare (48000.0, 512);
            e11.setRunning (true);
            for (int i = 0; i < 60; ++i) { blocks (e11, 8); juce::Thread::sleep (15); }      // the imports, in their own time
            ir.moveFileTo (tmp.getChildFile ("sf000_ir_moved.wav"));
            ir2.moveFileTo (tmp.getChildFile ("sf000_ir_moved_b.wav"));
            check (e11.insertType (7, 0) == 0, "it opens as it was left");
            s11.recall (snapB);
            const bool b = placeNames (e11, leadIns, false).joinIntoString (",").equalsIgnoreCase ("0:sf000_ir_test_b");
            blocks (e11, 6);
            s11.recall (snapA);
            const bool a = placeNames (e11, leadIns, false).joinIntoString (",").equalsIgnoreCase ("0:sf000_ir_test");
            check (a && b && ! e11.fault.load(), "after the app opens again, both snapshots bring their convolution back at once, without the disk");
            tmp.getChildFile ("sf000_ir_moved.wav").moveFileTo (ir);
            tmp.getChildFile ("sf000_ir_moved_b.wav").moveFileTo (ir2);
        }
        Session::stateFileForTests = juce::File();
        tmp.getChildFile ("sf000_ui_test").deleteRecursively();
        ir.deleteFile(); ir2.deleteFile();
    }
    // ---- quitting while the library on the web has stopped answering: the send is called off, the app leaves
    {
        // a library that takes the call and then says nothing, for as long as it is left to
        struct Mute : juce::Thread
        {
            Mute() : juce::Thread ("mute library") { ok = listener.createListener (0, "127.0.0.1"); port = listener.getBoundPort(); startThread(); }
            ~Mute() override { signalThreadShouldExit(); listener.close(); stopThread (4000); }
            void run() override
            {
                while (! threadShouldExit())
                    if (auto* c = listener.waitForNextConnection()) { held.add (c); ++calls; }
            }
            juce::StreamingSocket listener; juce::OwnedArray<juce::StreamingSocket> held;
            std::atomic<int> calls { 0 }; int port = 0; bool ok = false;
        };
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_quit_test");
        for (int way = 0; way < 2; ++way)
        {
            tmp.deleteRecursively();
            Session::stateFileForTests = tmp.getChildFile ("session.json");
            Online::systemClientForTests = way == 1;
            Mute mute;
            Engine e2;
            e2.prepare (48000.0, 128);
            Session s2 (e2, true);
            juce::AudioBuffer<float> b2 (2, 128);
            for (int k = 0; k < 400; ++k) e2.render (b2);
            s2.take(); s2.finishTakes (true);
            double tookMs = -1.0; int calls = 0;
            {
                auto on = std::make_unique<Online> (s2);
                Online::Setup st; st.url = "http://127.0.0.1:" + juce::String (mute.port); st.clientId = "id"; st.clientSecret = "secret"; st.on = true;
                on->setSetup (st);
                on->startSending();
                // (the sender reads the session under the message thread's lock: this thread keeps answering)
                for (int i = 0; i < 300 && mute.calls.load() == 0; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (150);       // it has asked, and is waiting on an answer that will not come
                calls = mute.calls.load();
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                on.reset();
                tookMs = juce::Time::getMillisecondCounterHiRes() - t0;
            }
            check (mute.ok && calls >= 1 && tookMs >= 0.0 && tookMs < 1500.0,
                   (juce::String (way == 0 ? "quitting with a send stalled (the library took the call and never answered): the send is called off and the sender is gone in "
                                          : "the same through the system's own client, as https goes: gone in ") + juce::String ((int) std::lround (tookMs)) + " ms (it waited 8 s and was then killed by force)").toRawUTF8());
        }
        Online::systemClientForTests = false;
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
    }
    std::printf (bad == 0 ? "ALL SCREEN CHECKS PASSED\n" : "%d SCREEN CHECK(S) FAILED\n", bad);
    return bad;
}

static int shot (const juce::StringArray& args)
{
    using namespace sf0;
    auto arg = [&] (const char* name, juce::String def) { const int i = args.indexOf (name); return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : def; };
    Engine e;
    e.prepare (48000.0, 512);
    Session ses (e, false);
    ses.activeLane = juce::jlimit (0, kLanes - 1, arg ("--lane", "8").getIntValue() - 1);
    if (ses.activeLane != 7) ses.selStep = -1;
    e.setRunning (true);
    juce::AudioBuffer<float> b (2, 512);
    for (int i = 0; i < 700; ++i) e.render (b);          // a few bars in, so the meters and the playing step show
    if (args.contains ("--snap")) ses.take();
    if (args.contains ("--profiled"))
    {
        // a library with sound kept in it: each snapshot it comes with played and taken again, and some plainer states
        auto bars = [&] (double n) { const int blocks = (int) (n * 4.0 * 60.0 / e.tempo() * 48000.0 / 512.0); for (int k = 0; k < blocks; ++k) e.render (b); };
        const auto first = ses.library;
        for (auto& s : first) { ses.recall (s.id); bars (7.0); ses.take(); }
        auto only = [&] (std::initializer_list<int> keep)
        {
            for (auto& s : first) if (s.name == "INIT") ses.recall (s.id);
            for (int l = 0; l < kLanes; ++l) ses.userSet (cx::lane (l, cx::kMute), std::find (keep.begin(), keep.end(), l) != keep.end() ? 0.0f : 1.0f);
            bars (7.0); ses.take();
            for (int l = 0; l < kLanes; ++l) ses.userSet (cx::lane (l, cx::kMute), 0.0f);
        };
        only ({ 0 }); only ({ 0, 5 }); only ({ 0, 1, 2, 3, 4 }); only ({ 9, 10 }); only ({ 7 }); only ({ 1, 2 }); only ({ 0, 5, 6, 9 }); only ({ 1, 2, 3, 4, 7, 8 });
        for (auto& s : first) if (s.name == "INIT") ses.recall (s.id);
        bars (2.0);
    }
    if (args.contains ("--glide"))
    {
        // a snapshot knob turned with GLIDE at 4 bars, caught on the way
        ses.userSet (cx::kGlide, 5.0f / 11.0f);
        ses.turnSnapKnob (0, 1.0f);
        ses.turnSnapKnob (3, 0.8f);
        for (int i = 0; i < 220; ++i) e.render (b);
    }
    if (arg ("--space", "") == "conv") e.setSpaceModel (1);
    if (args.contains ("--seqmute")) { ses.userSet (cx::lane (ses.activeLane, cx::kSeqMute), 1.0f); for (int i = 0; i < 8; ++i) e.render (b); }
    Screen sc (ses);
    const auto page = arg ("--page", "main");
    sc.setPage (page == "library" ? Screen::pLibrary : page == "macros" ? Screen::pMacros : Screen::pMain);
    const auto cfg = arg ("--cfg", "");
    if (cfg == "lane") sc.showConfig ({ 0, ses.activeLane, 0 });
    else if (cfg == "insert") sc.showConfig ({ 1, ses.activeLane, 0 });
    else if (cfg == "return") sc.showConfig ({ 2, 0, 0 });
    else if (cfg == "space") sc.showConfig ({ 2, 0, 1 });
    if (cfg.isNotEmpty() && args.contains ("--load")) sc.showLoad (arg ("--load", ""));         // LOAD's list, open on that folder
    // --rec: a take under way (in a folder of the system's temporary ones, gone again afterwards)
    const auto recDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_shot_rec");
    Recorder rec (e);
    rec.root = recDir;
    sc.recorder = &rec;
    if (args.contains ("--rec")) { rec.start(); for (int i = 0; i < 94 * 75; ++i) e.render (b); }
    // --online off|setup|sending|sent|waiting|refused: the library's strip with ONLINE as it would stand
    std::unique_ptr<Online> online;
    const auto onlineDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_shot_online");
    if (args.contains ("--online"))
    {
        Session::stateFileForTests = onlineDir.getChildFile ("session.json");
        online = std::make_unique<Online> (ses);
        const auto how = arg ("--online", "off");
        online->showForTests (how == "setup" ? Online::State::notSetUp : how == "sending" ? Online::State::working : how == "sent" ? Online::State::upToDate
                              : how == "waiting" ? Online::State::waiting : how == "refused" ? Online::State::refused : Online::State::off,
                              how == "sending" || how == "waiting" || how == "off" ? 3 : 0, how == "sent" ? 41 : 12, how == "refused" ? "403" : "");
        sc.online = online.get();
    }
    const float scale = (float) juce::jlimit (0.25, 4.0, arg ("--scale", "2").getDoubleValue());
    sc.setSize ((int) Screen::kW, (int) Screen::kH);
    if (args.contains ("--flat")) sc.setLit (false);            // the flat drawing the lit skin replaced
    if (args.contains ("--dark")) sc.setDark (true);            // the lit skin's dark instrument
    if (args.contains ("--info")) sc.setInfo (true);            // INFO: the MIDI numbers
    if (args.contains ("--phones")) e.setPhones (true);
    if (const auto pt = arg ("--point", ""); pt.isNotEmpty())   // the mouse resting at x,y (screen units), with INFO on: its box
    {
        (void) sc.createComponentSnapshot (sc.getLocalBounds(), false, scale);
        sc.testPointAt ({ pt.upToFirstOccurrenceOf (",", false, false).getFloatValue(), pt.fromFirstOccurrenceOf (",", false, false).getFloatValue() });
    }
    juce::Image img;
    if (args.contains ("--3d"))
    {
        // The 3D stage, as drawn: --view top|player|hero (or --turn and --tilt degrees, --away units),
        // --size WxH (the window, in points). It needs a window to draw in.
        const auto size = arg ("--size", "1728x1117");
        sc.setSize (juce::jlimit (320, 8000, size.upToFirstOccurrenceOf ("x", false, true).getIntValue()), juce::jlimit (200, 8000, size.fromFirstOccurrenceOf ("x", false, true).getIntValue()));
        sc.addToDesktop (0);
        sc.setVisible (true);
        Screen::Eye eye;
        const auto view = arg ("--view", "player");
        eye.pose.view = view == "top" ? 0 : view == "hero" ? 2 : 1;
        eye.pose.turn = eye.pose.view == 2 ? -30.0f : 0.0f; eye.pose.tilt = eye.pose.view == 0 ? 89.9f : eye.pose.view == 1 ? 56.0f : 31.0f;
        if (args.contains ("--turn")) eye.pose.turn = arg ("--turn", "0").getFloatValue();
        if (args.contains ("--tilt")) eye.pose.tilt = arg ("--tilt", "56").getFloatValue();
        sc.setEye (eye);
        if (const int gi = args.indexOf ("--glow"); gi >= 0 && gi + 1 < args.size())
        {
            const auto hb = juce::StringArray::fromTokens (args[gi + 1], ",", "");
            if (hb.size() == 2) sc.setGlow (hb[0].getFloatValue(), hb[1].getFloatValue());
        }
        sc.setThree (true);
        for (int i = 0; i < 400 && sc.isThree() && ! sc.threeShowing(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        if (! sc.threeShowing()) { std::printf ("the 3D stage did not start: %s\n", sc.threeNote().toRawUTF8()); return 1; }
        if (args.contains ("--look"))
        {
            // --look x,y,w,h: the eye goes to hold that much of the screen (units), as it does when following
            juce::StringArray n; n.addTokens (arg ("--look", ""), ",", "");
            if (n.size() == 4) { const juce::Rectangle<float> box (n[0].getFloatValue(), n[1].getFloatValue(), n[2].getFloatValue(), n[3].getFloatValue()); sc.stageOrNull()->lookAt (&box); }
        }
        sc.refreshNow();
        sc.stageOrNull()->settle();
        (void) sc.stageOrNull()->capture();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        sc.refreshNow();
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        img = sc.stageOrNull()->capture();
        std::printf ("3D: %s; a frame %.1f ms; the screen recorded and nothing to print %.2f ms; the first picture's darkest to brightest %d of 255\n", sc.stageOrNull()->driver().toRawUTF8(), sc.stageOrNull()->frameMs(), t1 - t0, sc.stageOrNull()->firstPictureSpread());
        sc.setThree (false);
        sc.removeFromDesktop();
    }
    else img = sc.createComponentSnapshot (sc.getLocalBounds(), false, scale);
    rec.stop(); sc.recorder = nullptr; recDir.deleteRecursively();
    sc.online = nullptr; online = nullptr; Session::stateFileForTests = juce::File(); onlineDir.deleteRecursively();
    juce::File f (arg ("--shot", "sf000.png"));
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (img, out);
    std::printf (ok ? "wrote %s (%d x %d)\n" : "could not write %s\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok ? 0 : 1;
}

// The snapshot knobs' names for the E16's page: from the session on disk if there is one (as named
// in the macro designer), else the ones the rig starts with.
static juce::StringArray snapKnobNames()
{
    juce::StringArray names;
    const juce::var root = juce::JSON::parse (sf0::Session::stateFile());
    if (auto* kn = root.getProperty ("knobs", juce::var()).getArray())
        for (auto& k : *kn) names.add (k.getProperty ("name", "").toString());
    if (names.isEmpty())
    {
        sf0::Engine e;
        sf0::Session s (e, false);
        for (auto& k : s.knobs) names.add (k.name);
    }
    while (names.size() < 16) names.add ({});
    return names;
}

static int writeScene (const juce::String& path)
{
    sf0::Engine e;
    const auto text = sf0::e16::sceneText (e, snapKnobNames());
    const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    const bool ok = f.replaceWithData (text.toRawUTF8(), text.getNumBytesAsUTF8());
    std::printf (ok ? "wrote %s (%d bytes): copy it into the OXI App's Scenes folder and send it to the E16\n" : "could not write %s\n",
                 f.getFullPathName().toRawUTF8(), (int) text.getNumBytesAsUTF8());
    // the library's scene beside it
    const auto lib = sf0::e16::libraryText();
    const auto lf = f.getSiblingFile (f.getFileNameWithoutExtension() + "-LIBRARY" + f.getFileExtension());
    const bool ok2 = lf.replaceWithData (lib.toRawUTF8(), lib.getNumBytesAsUTF8());
    std::printf (ok2 ? "wrote %s (%d bytes): the library's scene (the map, the finders)\n" : "could not write %s\n", lf.getFullPathName().toRawUTF8(), (int) lib.getNumBytesAsUTF8());
    return ok && ok2 ? 0 : 1;
}

// Notes in, the E16's knobs, presses and rings, the clock out, patterns on the bar line: offline,
// with messages made here in place of the hardware.
static int midiTest()
{
    using namespace sf0;
    int bad = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++bad; };
    std::printf ("SF-000 MIDI checks\n");

    // 1. the E16's addresses
    {
        std::set<int> seen;
        bool round = true, clean = true;
        for (int slot = 0; slot < e16::kSlots; ++slot)
        {
            int ch = 0, num = 0;
            e16::addressOf (slot, ch, num);
            round = round && e16::slotFor (ch, num) == slot && e16::controlFor (slot) >= 0 && e16::controlFor (slot) < cx::kCount;
            clean = clean && seen.insert (ch * 128 + num).second && num != 6 && num != 38 && ! (num >= 96 && num <= 101) && num < 120;
        }
        for (int k = 0; k < 16; ++k) clean = clean && seen.insert (e16::kSnapChannel * 128 + e16::kPressCc + k).second;
        clean = clean && seen.insert (e16::kSnapChannel * 128 + e16::kFocusCc).second;
        check (round && clean, juce::String (e16::kSlots) + " knob addresses, 16 presses and the page's own: each one thing, none on a number MIDI keeps for itself");
        Engine e;
        const auto& bip = e.control (cx::lane (0, 0));       // KICK TUNE: a knob with a centre
        const auto& uni = e.control (cx::lane (0, 1));
        bool maps = bip.kind == Kind::Bipolar && e16::from7 (bip, 64) == 0.5f && e16::to7 (bip, 0.5f) == 64 && e16::from7 (bip, 0) == 0.0f && e16::from7 (bip, 127) == 1.0f;
        for (int v = 0; v < 128; ++v) maps = maps && e16::to7 (bip, e16::from7 (bip, v)) == v && e16::to7 (uni, e16::from7 (uni, v)) == v;
        check (maps, "every one of a knob's 128 places comes back as itself; a knob with a centre is centred at 64 exactly");
    }

    // 2. notes in: a lane's number is its channel, and a note takes the same path as a step
    {
        auto firstSound = [] (int l, bool external, int at)
        {
            Engine e;
            e.prepare (48000.0, 512);
            Pattern p;
            if (! external) { p.steps[0].on = true; p.steps[0].vel = 1.0f; p.steps[0].gate = 2.0f; p.steps[0].octave = lanes()[(size_t) l].octave; }
            for (int k = 0; k < kLanes; ++k) e.setPattern (k, k == l ? p : Pattern());
            for (int k = 0; k < kLanes; ++k) for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (k, sl, -1);
            for (int k = 0; k < cx::kCount; ++k) if (e.control (k).id.endsWith (".attack")) e.set (k, 0.0f);
            e.solo = l;
            e.setRunning (true);
            const int note = 12 * (lanes()[(size_t) l].octave + 2);
            if (external) { e.noteInAt (juce::MidiMessage::noteOn (l + 1, note, (juce::uint8) 127), at); e.noteInAt (juce::MidiMessage::noteOff (l + 1, note), at + 9000); }
            juce::AudioBuffer<float> b (2, 512);
            int on = -1;
            for (int done = 0; done < 48000 && on < 0; done += 512)
            {
                e.render (b);
                for (int i = 0; i < 512 && on < 0; ++i) if (std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i)) > 2e-3f) on = done + i;
            }
            return on;
        };
        int worst = 0, silent = 0;
        juce::String list;
        for (int l = 0; l < kLanes; ++l)
        {
            const int a = firstSound (l, false, 0), b = firstSound (l, true, 0);
            if (b < 0) ++silent;
            worst = std::max (worst, std::abs (a - b));
            list << " " << (b - a);
        }
        check (silent == 0 && worst <= 2, "a note on a lane's channel sounds that lane, on the same sample a step would (differences, in samples:" + list + ")");
        const int k0 = firstSound (0, true, 0), k1 = firstSound (0, true, 300);
        check (k1 - k0 == 300, "300 samples later in, 300 samples later out");
        // a note that only says when it came: one block later, at the same place in the block
        auto arrived = [] (double ms)
        {
            Engine e;
            e.prepare (48000.0, 512);
            for (int k = 0; k < kLanes; ++k) { e.setPattern (k, Pattern()); for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (k, sl, -1); }
            e.solo = 0;
            juce::AudioBuffer<float> b (2, 512);
            for (int i = 0; i < 4; ++i) e.render (b);                  // blocks 0-3: the fourth began at 32 ms
            e.noteIn (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 32.0 + ms);
            int on = -1;
            for (int done = 4 * 512; done < 48000 && on < 0; done += 512)
            {
                e.render (b);
                for (int i = 0; i < 512 && on < 0; ++i) if (std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i)) > 2e-3f) on = done + i;
            }
            return on;
        };
        const int a1 = arrived (1.0), a2 = arrived (4.7), ref = firstSound (0, true, 0);
        check (std::abs ((a2 - a1) - 178) <= 1 && std::abs ((a1 - ref) - (4 * 512 + 48)) <= 1,
               "notes 3.7 ms apart on the way in are 3.7 ms apart in the sound (" + juce::String (a2 - a1) + " samples), in the block after the one they came during");
    }

    // 2b. a lane's own sequencer muted: its steps go unplayed, what it held ends, notes from outside
    //     still sound, and it comes back on a step
    {
        auto rig = [] (Engine& e, int l, const Pattern& p)
        {
            e.prepare (48000.0, 512);
            for (int k = 0; k < kLanes; ++k) { e.setPattern (k, k == l ? p : Pattern()); for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (k, sl, -1); }
            e.set (cx::lane (l, cx::kSendA), 0.0f); e.set (cx::lane (l, cx::kSendB), 0.0f);
            e.set (cx::kTempo, tempoToValue (120.0));           // a beat is 24000 samples
            e.solo = l;
            juce::AudioBuffer<float> w (2, 512);
            e.render (w);                                       // the tempo is taken up before it runs
            e.setRunning (true);
        };
        juce::AudioBuffer<float> b (2, 512);
        // peak over some blocks, and where the sound first rose above the floor (-1: it did not)
        auto run = [&b] (Engine& e, int blocks, int& done, int* onset = nullptr)
        {
            float peak = 0.0f;
            if (onset != nullptr) *onset = -1;
            for (int k = 0; k < blocks; ++k, done += 512)
            {
                e.render (b);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i));
                    peak = std::max (peak, v);
                    if (onset != nullptr && *onset < 0 && v > 2e-3f) *onset = done + i;
                }
            }
            return peak;
        };
        Pattern four;
        for (int k = 0; k < 16; k += 4) { four.steps[(size_t) k].on = true; four.steps[(size_t) k].vel = 1.0f; }
        Engine e;
        rig (e, 0, four);
        const int mute = cx::lane (0, cx::kSeqMute);
        int done = 0, first = -1, ext = -1, back = -1;
        const float loud = run (e, 188, done, &first);          // a bar of kicks
        e.set (mute, 1.0f);
        run (e, 400, done);                                     // the last kick dies away, and the limiter's lift of it
        const float quiet = run (e, 188, done);                 // a bar with every step unplayed
        check (loud > 0.1f && quiet < 2e-3f && e.isRunning(), "SEQ MUTE: the lane's steps go unplayed while the transport runs on (peak " + juce::String (loud, 3) + " to " + juce::String (quiet, 5) + ")");
        e.noteInAt (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 100);
        e.noteInAt (juce::MidiMessage::noteOff (1, 60), 100 + 4000);
        const int extAt = done + 100;
        const float outside = run (e, 100, done, &ext);
        check (outside > 0.1f && ext - extAt == first, "a note from outside still sounds on the muted lane, as soon as a step would (" + juce::String (ext - extAt) + " samples, a step " + juce::String (first) + ")");
        run (e, 431, done);                                     // it dies away again; then somewhere between two steps
        const int offAt = done;
        e.set (mute, 0.0f);
        run (e, 188, done, &back);
        const int grid = ((back - first) % 24000 + 24000) % 24000, off = std::min (grid, 24000 - grid);
        check (back >= 0 && off <= 2 && back - first > offAt - 512 && back - first < offAt + 24000,
               "unmuted between steps, it comes back on its next lit step, on the grid (" + juce::String (off) + " samples off, " + juce::String (back - first - offAt) + " after the key)");

        // a held note: the mute ends it, where the unmuted lane goes on sounding
        float tail[2] = { 0, 0 };
        int lead = 0;
        for (int l = 0; l < kLanes; ++l) if (juce::String (lanes()[(size_t) l].name) == "LEAD") lead = l;
        Pattern held;
        held.steps[0].on = true; held.steps[0].vel = 1.0f; held.steps[0].gate = 16.0f; held.steps[0].octave = lanes()[(size_t) lead].octave;
        for (int m = 0; m < 2; ++m)
        {
            Engine h;
            rig (h, lead, held);
            int d2 = 0;
            run (h, 60, d2);                                    // into the first note
            if (m == 1) h.set (cx::lane (lead, cx::kSeqMute), 1.0f);
            run (h, 60, d2);                                    // its release
            tail[m] = run (h, 30, d2);                          // still inside the first bar
        }
        check (tail[0] > 0.05f && tail[1] < 2e-3f, "muted while it holds a note, the note is let go (peak " + juce::String (tail[0], 3) + " held, " + juce::String (tail[1], 5) + " muted)");
    }

    // 2c. the returns are on the grid: the dub delay's first echo is its division after the hit
    {
        auto onsetOf = [] (bool echo)
        {
            Engine e;
            e.prepare (48000.0, 128);
            Pattern one;
            one.steps[0].on = true; one.steps[0].vel = 1.0f; one.steps[0].gate = 0.5f; one.length = 64;
            for (int k = 0; k < kLanes; ++k) { e.setPattern (k, k == 1 ? one : Pattern()); for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (k, sl, -1); }
            for (int k = 0; k < kLanes; ++k) { e.set (cx::lane (k, cx::kSendA), k == 1 && echo ? 1.0f : 0.0f); e.set (cx::lane (k, cx::kSendB), 0.0f); }
            e.set (cx::kTempo, tempoToValue (120.0));
            if (echo) e.set (cx::kRetMS + 1, 1.0f);            // the delay return alone: no dry sound
            juce::AudioBuffer<float> b (2, 128);
            for (int k = 0; k < 400; ++k) e.render (b);          // settled, as the app is before RUN
            e.setRunning (true);
            float peak = 0.0f;
            std::vector<float> mono;
            for (int k = 0; k < 300; ++k) { e.render (b); for (int i = 0; i < 128; ++i) { mono.push_back (std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i))); peak = std::max (peak, mono.back()); } }
            for (size_t i = 0; i < mono.size(); ++i) if (mono[i] > 0.25f * peak) return (int) i;
            return -1;
        };
        const int dry = onsetOf (false), wet = onsetOf (true);
        const double off = (double) (wet - dry - 18000) / 48.0;          // DUB TIME starts on a dotted eighth: 18000 samples at 120
        check (dry >= 0 && wet > dry && std::abs (off) < 2.5, "the dub delay's first echo comes a dotted eighth after the hit, to " + juce::String (off, 1) + " ms (it was 10.7 ms late while the returns held their sound back)");
    }

    // 3. the clock out
    {
        auto kickAt = [] (bool clock, float lead, std::vector<Engine::ClockMsg>* got, double bpmAfter = 0.0)
        {
            Engine e;
            e.prepare (48000.0, 128);
            e.setClockOut (clock, lead);
            e.solo = 0;
            juce::AudioBuffer<float> b (2, 128);
            for (int i = 0; i < 40; ++i) e.render (b);                 // stopped: no clock
            Engine::ClockMsg m;
            int freeTicks = 0;
            while (e.nextClock (m)) ++freeTicks;
            e.setRunning (true);
            int on = -1;
            for (int done = 40 * 128; done < 40 * 128 + 96000; done += 128)
            {
                if (bpmAfter > 0.0 && done == 40 * 128 + 48000) e.set (cx::kTempo, tempoToValue (bpmAfter));
                e.render (b);
                for (int i = 0; i < 128 && on < 0; ++i) if (std::abs (b.getSample (0, i)) > 2e-3f) on = done + i - 40 * 128;
                while (e.nextClock (m)) if (got != nullptr) got->push_back (m);
            }
            e.setRunning (false);
            e.render (b);
            while (e.nextClock (m)) if (got != nullptr) got->push_back (m);
            return std::pair<int, int> (on, freeTicks);
        };
        std::vector<Engine::ClockMsg> got;
        const auto plain = kickAt (false, 0.0f, nullptr), led = kickAt (true, 4.0f, &got);
        const int wait = 2 * 128 + (int) std::lround (0.004 * 48000.0);
        check (led.first - plain.first == wait, "with the clock on, RUN waits two blocks and LEAD before the first sound (" + juce::String (led.first - plain.first) + " samples at 4 ms)");
        check (led.second == 0, "stopped, nothing is sent: the clock begins with START, as Live's did");
        const double tick = 60000.0 / (132.0 * 24.0);
        bool order = got.size() > 50 && got[0].byte == 0xFA && got[1].byte == 0xF8 && got.back().byte == 0xFC;
        double worst = 0.0;
        int ticks = 0;
        for (size_t i = 2; i + 1 < got.size(); ++i) { order = order && got[i].byte == 0xF8; worst = std::max (worst, std::abs ((got[i].dueMs - got[i - 1].dueMs) - tick)); ++ticks; }
        check (order && worst < 0.02, "START, then a tick every " + juce::String (tick, 3) + " ms to within " + juce::String (worst, 4) + " ms (" + juce::String (ticks + 1) + " ticks in two seconds), then STOP");
        // The first tick against the first beat's sound: sent a block and LEAD before that beat is rendered.
        const double beatRendered = (40 * 128 + wait) * 1000.0 / 48000.0, ahead = beatRendered - (got.size() > 1 ? got[1].dueMs : 0.0);
        check (std::abs (ahead - (128 * 1000.0 / 48000.0 + 4.0)) < 0.05 && got[1].dueMs - got[0].dueMs == 1.0,
               "the first tick leaves " + juce::String (ahead, 2) + " ms before its beat is rendered: one block for the note's way in, and LEAD");
        std::vector<Engine::ClockMsg> turned;
        kickAt (true, 4.0f, &turned, 90.0);
        double lo = 1e9, hi = 0.0;
        for (size_t i = 2; i + 1 < turned.size(); ++i) { const double d = turned[i].dueMs - turned[i - 1].dueMs; lo = std::min (lo, d); hi = std::max (hi, d); }
        check (lo > tick - 0.05 && hi < 60000.0 / (90.0 * 24.0) + 0.05, "a tempo change slows the ticks without a gap or a burst (" + juce::String (lo, 2) + " to " + juce::String (hi, 2) + " ms)");
        // faster, with a long LEAD (the head start is then more beats: it was made up at once, with a tick due before the one sent ahead of it)
        std::vector<Engine::ClockMsg> faster;
        kickAt (true, 40.0f, &faster, 200.0);
        lo = 1e9; hi = 0.0;
        for (size_t i = 2; i + 1 < faster.size(); ++i) { const double d = faster[i].dueMs - faster[i - 1].dueMs; lo = std::min (lo, d); hi = std::max (hi, d); }
        const double tick200 = 60000.0 / (200.0 * 24.0);
        check (lo > tick200 * 0.5 - 0.05 && hi < tick + 0.05, "a jump to 200 with LEAD at 40 ms: every tick after the one before it, never nearer than half a tick (" + juce::String (lo, 2) + " to " + juce::String (hi, 2) + " ms)");
        // the clock switched on while it runs, and the audio started again while it runs
        {
            Engine e;
            e.prepare (48000.0, 128);
            e.solo = 0;
            juce::AudioBuffer<float> b (2, 128);
            Engine::ClockMsg m;
            e.setRunning (true);
            for (int i = 0; i < 1000; ++i) e.render (b);               // 2.7 seconds in, no clock
            e.setClockOut (true, 4.0f);
            e.render (b);
            int first = 0, starts = 0; std::vector<Engine::ClockMsg> after;
            while (e.nextClock (m)) { ++first; if (m.byte != 0xF8) ++starts; after.push_back (m); }
            for (int i = 0; i < 400; ++i) { e.render (b); while (e.nextClock (m)) after.push_back (m); }
            double w = 0.0;
            for (size_t i = 1; i < after.size(); ++i) w = std::max (w, std::abs ((after[i].dueMs - after[i - 1].dueMs) - tick));
            check (first <= 2 && starts == 0 && after.size() > 50 && w < 0.02, "the clock switched on with the rig running takes up from where the rig is: " + juce::String (first) + " in its first block (142 before, every tick since bar 1), then one every " + juce::String (tick, 3) + " ms");
            e.prepare (48000.0, 128);                                  // the audio stops and starts under it
            e.render (b);
            int again = 0; while (e.nextClock (m)) ++again;
            check (again <= 3, "and the audio started again under a running clock sends no burst (" + juce::String (again) + " in the first block)");
        }
    }

    // 3b. following an outside clock: its START, STOP and CONTINUE, its tempo, its beat
    {
        struct Ev { double ms; juce::uint8 byte; };
        struct Heard { std::vector<double> on; bool running = false, following = false; double tempo = 0.0, after = 0.0, late = 0.0; int lateN = 0; };
        // ticks at a tempo from a moment on; returns the moment after the last one's beat-part
        auto ticks = [] (std::vector<Ev>& v, double from, double bpm, int count, double jitter, juce::Random& r)
        {
            const double per = 60000.0 / (24.0 * bpm);
            for (int i = 0; i < count; ++i) v.push_back ({ from + (double) i * per + (jitter > 0.0 ? (r.nextDouble() * 2.0 - 1.0) * jitter : 0.0), 0xF8 });
            return from + (double) count * per;
        };
        // The closed hat on every eighth (or silent, with notes played in from outside), 128 samples
        // a block unless said, the interface taken as 4 ms. Returns when each hit began, in ms of the output.
        auto play = [] (const std::vector<Ev>& ev, double ms, int mode /* 0 follow off, 1 with notes in, 2 on the beat */, float trim,
                        const std::vector<double>& notes = {}, std::function<void (sf0::Engine&, double)> each = nullptr, int n = 128)
        {
            Engine e;
            e.prepare (48000.0, n);
            Pattern eighths;
            for (int k = 0; k < 16; k += 2) { eighths.steps[(size_t) k].on = true; eighths.steps[(size_t) k].vel = 1.0f; eighths.steps[(size_t) k].gate = 0.5f; }
            for (int k = 0; k < kLanes; ++k) { e.setPattern (k, k == 1 && (notes.empty() || mode != 0) ? eighths : Pattern()); for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (k, sl, -1); }
            e.set (cx::lane (1, cx::kSendA), 0.0f); e.set (cx::lane (1, cx::kSendB), 0.0f);
            e.solo = 1;
            e.setFollow (mode != 0, mode == 2, trim);
            e.setOutputDelayForTests (4.0f);
            juce::AudioBuffer<float> b (2, n);
            const double dur = (double) n * 1000.0 / 48000.0;
            size_t nextEv = 0, nextNote = 0;
            Heard h;
            int quiet = 100000;
            for (int k = 0; (double) k * dur < ms; ++k)
            {
                const double now = (double) k * dur;                    // the engine's own clock, with no device: the samples
                while (nextEv < ev.size() && ev[nextEv].ms <= now) { e.clockIn (ev[nextEv].byte, ev[nextEv].ms); ++nextEv; }
                while (nextNote < notes.size() && notes[nextNote] <= now)
                {
                    e.noteIn (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 127), notes[nextNote]);
                    e.noteIn (juce::MidiMessage::noteOff (2, 60), notes[nextNote] + 40.0);
                    ++nextNote;
                }
                if (each) each (e, now);
                e.render (b);
                for (int i = 0; i < n; ++i)
                {
                    const bool loud = std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i)) > 0.03f;      // (well above the hat's tail)
                    if (loud && quiet > 2400) h.on.push_back ((double) (k * n + i) / 48.0);
                    quiet = loud ? 0 : quiet + 1;
                }
            }
            h.running = e.isRunning(); h.following = e.following(); h.tempo = e.tempo(); h.after = e.soundsAfterMs();
            h.late = e.notesLateMs(); h.lateN = e.notesHeard();
            return h;
        };
        juce::Random rnd (7);
        const double beat = 60000.0 / 126.0;

        // how long after it arrives a note from outside begins, on the output: the yardstick
        std::vector<double> played;
        for (int k = 0; k < 16; ++k) played.push_back (100.0 + (double) k * beat / 2.0);
        const auto ref = play ({}, 4500.0, 0, 0.0f, played);
        double lat = 0.0, latSpread = 0.0;
        for (size_t k = 0; k < ref.on.size() && k < played.size(); ++k) lat += (ref.on[k] - played[k]) / (double) std::min (ref.on.size(), played.size());
        for (size_t k = 0; k < ref.on.size() && k < played.size(); ++k) latSpread = std::max (latSpread, std::abs (ref.on[k] - played[k] - lat));
        check (ref.on.size() == played.size() && latSpread < 0.2, "the yardstick: a note from outside begins " + juce::String (lat, 2) + " ms after it arrives, give or take " + juce::String (latSpread, 2));
        // how far each hit is from where a note the sender played on that eighth would be (ms); `from`: the first eighth counted
        auto errors = [&] (const Heard& h, double first, double perEighth, double shift, size_t from, size_t upTo = 100000)
        {
            double worst = 0.0;
            for (size_t k = from; k < h.on.size() && k < upTo; ++k) worst = std::max (worst, std::abs (h.on[k] - (first + (double) k * perEighth + lat + shift)));
            return worst;
        };

        // A. START, then a steady clock: each eighth where a note the sender played then would be
        std::vector<Ev> a { { 50.0, 0xFA } };
        ticks (a, 51.0, 126.0, 24 * 12, 0.0, rnd);
        const auto ha = play (a, 5000.0, 1, 0.0f);
        check (ha.on.size() == 21 && ha.running && ha.following && std::abs (ha.on[0] - (51.0 + lat)) < 0.2 && errors (ha, 51.0, beat / 2.0, 0.0, 0) < 0.3,
               "START and a steady 126: it runs, bar 1's first step sounds on the first tick, and every eighth is where the sender's own note would be (worst "
                   + juce::String (errors (ha, 51.0, beat / 2.0, 0.0, 0), 2) + " ms over " + juce::String ((int) ha.on.size()) + ")");
        check (std::abs (ha.tempo - 126.0) < 0.001, "TEMPO reads the sender's: " + juce::String (ha.tempo, 2));
        const auto hOff = play (a, 2000.0, 0, 0.0f);
        check (hOff.on.empty() && ! hOff.running && ! hOff.following, "with FOLLOW off, a clock coming in starts nothing");

        // B. the same clock with a millisecond of jitter on every tick, and the rig's tempo far from the sender's to begin with
        std::vector<Ev> bj { { 50.0, 0xFA } };
        ticks (bj, 51.0, 126.0, 24 * 20, 1.0, rnd);
        const auto hb = play (bj, 9000.0, 1, 0.0f);
        check (hb.on.size() >= 36 && errors (hb, 51.0, beat / 2.0, 0.0, 4) < 0.5,
               "ticks arriving up to 1 ms early or late: the eighths stay within " + juce::String (errors (hb, 51.0, beat / 2.0, 0.0, 4), 2) + " ms once it has settled ("
                   + juce::String (errors (hb, 51.0, beat / 2.0, 0.0, 0, 4), 2) + " ms in the first two beats)");
        check (std::abs (hb.tempo - 126.0) < 0.001, "and TEMPO still reads " + juce::String (hb.tempo, 2));

        // C. on the beat: its own steps are played ahead by the time the rig takes to sound
        const auto hc = play (a, 5000.0, 2, 0.0f);
        check (hc.on.size() == 21 && errors (hc, 51.0, beat / 2.0, -hc.after, 1) < 0.3 && hc.on[0] - (51.0 + lat) < 0.2 && hc.on[0] - (51.0 + lat) > -2.8,
               "ON THE BEAT: every eighth is " + juce::String (hc.after, 1) + " ms ahead of that (the buffer, the interface, the look-ahead: heard on the sender's beat), worst "
                   + juce::String (errors (hc, 51.0, beat / 2.0, -hc.after, 1), 2) + " ms; bar 1's first step cannot be, and is played at once, in the block the first tick is heard in (" + juce::String (hc.on[0] - (51.0 + lat), 2) + " ms)");
        const auto ht = play (a, 5000.0, 1, 6.0f);
        check (errors (ht, 51.0, beat / 2.0, -6.0, 1) < 0.3, "TRIM at 6 ms plays them 6 ms earlier (worst " + juce::String (errors (ht, 51.0, beat / 2.0, -6.0, 1), 2) + " ms)");

        // the panel's readout: where the sender's notes land against the rig's own steps
        std::vector<double> sent;
        for (int k = 4; k < 20; ++k) sent.push_back (51.0 + (double) k * beat / 2.0);
        const auto hn = play (a, 5000.0, 1, 0.0f, sent), hm = play (a, 5000.0, 2, 0.0f, sent);
        check (hn.lateN >= 12 && std::abs (hn.late) < 0.3 && hm.lateN >= 12 && std::abs (hm.late - hm.after) < 0.3,
               "NOTES IN LAND reads " + juce::String (hn.late, 2) + " ms with WITH NOTES IN and " + juce::String (hm.late, 1) + " ms late with ON THE BEAT: the head start the rig's own steps were given");

        {
            const auto hj = play (bj, 9000.0, 2, 0.0f);
            check (errors (hj, 51.0, beat / 2.0, -hj.after, 4) < 0.9, "ON THE BEAT under that jitter: within " + juce::String (errors (hj, 51.0, beat / 2.0, -hj.after, 4), 2) + " ms of the beat it is aiming " + juce::String (hj.after, 1) + " ms ahead for");
        }
        // the same at 512 samples a block, with jitter: a note from outside waits a longer buffer, and the head start grows with it
        {
            std::vector<double> p5;
            for (int k = 0; k < 8; ++k) p5.push_back (100.0 + (double) k * beat / 2.0);
            const auto r5 = play ({}, 2500.0, 0, 0.0f, p5, nullptr, 512);
            double lat5 = 0.0;
            for (size_t k = 0; k < r5.on.size() && k < p5.size(); ++k) lat5 += (r5.on[k] - p5[k]) / (double) std::min (r5.on.size(), p5.size());
            const auto h5 = play (bj, 9000.0, 2, 0.0f, {}, nullptr, 512);
            double worst5 = 0.0;
            for (size_t k = 4; k < h5.on.size(); ++k) worst5 = std::max (worst5, std::abs (h5.on[k] - (51.0 + (double) k * beat / 2.0 + lat5 - h5.after)));
            check (r5.on.size() == p5.size() && h5.on.size() >= 36 && worst5 < 0.9 && std::abs (h5.after - hc.after - 8.0) < 0.01,
                   "at 512 samples a block, ON THE BEAT under the same jitter: " + juce::String (h5.after, 1) + " ms ahead, within " + juce::String (worst5, 2) + " ms");
        }

        // D. the sender's tempo turned from 126 to 140 between two ticks
        std::vector<Ev> d { { 50.0, 0xFA } };
        const double turn = ticks (d, 51.0, 126.0, 24 * 4, 0.0, rnd);
        ticks (d, turn, 140.0, 24 * 14, 0.0, rnd);
        const auto hd = play (d, 7500.0, 1, 0.0f);
        double worstTurn = 0.0, worstAfter = 0.0;
        for (size_t k = 8; k < hd.on.size(); ++k)
        {
            const double err = std::abs (hd.on[k] - (turn + (double) (k - 8) * 60000.0 / 140.0 / 2.0 + lat));
            (k < 12 ? worstTurn : worstAfter) = std::max (k < 12 ? worstTurn : worstAfter, err);
        }
        check (hd.on.size() >= 30 && worstTurn < 4.0 && worstAfter < 0.3 && std::abs (hd.tempo - 140.0) < 0.001,
               "the sender turned from 126 to 140 at once: within " + juce::String (worstTurn, 2) + " ms over the next two beats, " + juce::String (worstAfter, 2) + " ms after them; TEMPO reads " + juce::String (hd.tempo, 2));

        // E. STOP, CONTINUE, START again
        std::vector<Ev> t { { 50.0, 0xFA } };
        const double stopAt = ticks (t, 51.0, 126.0, 24 * 2 + 6, 0.0, rnd);         // two beats and a sixteenth, then STOP
        t.push_back ({ stopAt - 5.0, 0xFC });
        bool stopped = false, resumed = false, again = false;
        t.push_back ({ stopAt + 800.0, 0xFB });
        const double contAt = stopAt + 801.0;
        const double restart = ticks (t, contAt, 126.0, 24 * 2, 0.0, rnd);          // CONTINUE: tick 54 next, the eighth at tick 60
        t.push_back ({ restart - 5.0, 0xFC });
        t.push_back ({ restart + 300.0, 0xFA });
        ticks (t, restart + 301.0, 126.0, 24 * 3, 0.0, rnd);
        const auto he = play (t, restart + 301.0 + 3.0 * beat - 30.0, 1, 0.0f, {}, [&] (sf0::Engine& e, double now)
        {
            if (now > stopAt + 100.0 && now < stopAt + 700.0) stopped = stopped || ! e.isRunning();
            if (now > contAt + 100.0 && now < restart) resumed = resumed || e.isRunning();
            if (now > restart + 400.0) again = again || e.isRunning();
        });
        // heard: eighths 0-4 (ticks 0, 12, 24, 36, 48), silence, then tick 60, 72, 84, 96 from the CONTINUE, then 0, 12, ... from the START
        std::vector<double> want;
        for (int k = 0; k < 5; ++k) want.push_back (51.0 + (double) k * beat / 2.0);
        for (int k = 60; k < 54 + 48; k += 12) want.push_back (contAt + (double) (k - 54) * beat / 24.0);
        for (int k = 0; k < 6; ++k) want.push_back (restart + 301.0 + (double) k * beat / 2.0);
        double worstE = 0.0;
        for (size_t k = 0; k < he.on.size() && k < want.size(); ++k) worstE = std::max (worstE, std::abs (he.on[k] - (want[k] + lat)));
        check (stopped && resumed && again && he.on.size() == want.size() && worstE < 0.3,
               "STOP stops it, CONTINUE takes it on from where it stopped, START takes it back to the top (" + juce::String ((int) he.on.size()) + " of " + juce::String ((int) want.size()) + " hits, worst " + juce::String (worstE, 2) + " ms)");

        // F. the clock gone without a STOP (a cable pulled): it runs on alone at that tempo
        std::vector<Ev> g { { 50.0, 0xFA } };
        const double goneAt = ticks (g, 51.0, 118.5, 24 * 4, 0.0, rnd);
        const auto hg = play (g, goneAt + 3000.0, 1, 0.0f);
        double worstG = 0.0;
        for (size_t k = 0; k < hg.on.size(); ++k) worstG = std::max (worstG, std::abs (hg.on[k] - (51.0 + (double) k * 60000.0 / 118.5 / 2.0 + lat)));
        check (hg.running && ! hg.following && hg.on.size() >= 19 && worstG < 1.5 && std::abs (hg.tempo - 118.5) < 0.001,
               "the clock gone without a STOP: it plays on alone at " + juce::String (hg.tempo, 1) + ", " + juce::String (worstG, 2) + " ms from where the sender would have been three seconds on");

        // H. a sender that sends ticks while it is stopped, and starts them afresh with START
        {
            std::vector<Ev> h;
            const double idleEnd = ticks (h, 10.0, 126.0, 40, 0.0, rnd);
            const double go = idleEnd - 12.0;                               // START 12 ms before the tick that was due: its first tick comes 1 ms after it
            while (! h.empty() && h.back().ms >= go) h.pop_back();
            h.push_back ({ go, 0xFA });
            ticks (h, go + 1.0, 126.0, 24 * 8, 0.0, rnd);
            bool early = false;
            const auto hh = play (h, go + 3500.0, 1, 0.0f, {}, [&] (sf0::Engine& e, double now) { if (now < go - 5.0) early = early || e.isRunning(); });
            check (! early && hh.on.size() >= 14 && errors (hh, go + 1.0, beat / 2.0, 0.0, 0) < 0.3,
                   "ticks sent while the sender is stopped start nothing; its START and the fresh tick after it are bar 1 (worst " + juce::String (errors (hh, go + 1.0, beat / 2.0, 0.0, 0), 2) + " ms)");
        }

        // I. RUN by hand while the sender plays on: stopped here, started here, back in at the sender's place
        {
            double posErr = 1.0e9, lastPos = 0.0;
            const auto hi = play (a, 5000.0, 1, 0.0f, {}, [&] (sf0::Engine& e, double now)
            {
                if (std::abs (now - 1200.0) < 1.4) e.setRunning (false);
                if (std::abs (now - 2300.0) < 1.4) e.setRunning (true);
                if (now > 4000.0) { lastPos = e.songPos(); posErr = std::abs (lastPos - (now - 51.0) / beat); }
            });
            double worstI = 0.0; int inGap = 0;
            for (auto on : hi.on)
            {
                if (on > 1300.0 && on < 2300.0) ++inGap;
                const double q = (on - lat - 51.0) / (beat / 2.0);
                worstI = std::max (worstI, std::abs (q - std::round (q)) * beat / 2.0);
            }
            check (inGap == 0 && hi.running && worstI < 0.3 && posErr < 0.02,
                   "stopped and started by hand under a running clock: silent between, then back on the sender's eighths (worst " + juce::String (worstI, 2) + " ms) at the sender's place in the bar (beat "
                       + juce::String (lastPos, 2) + ")");
        }

        // J. one tick delivered 8 ms late, the next on time
        {
            std::vector<Ev> l { { 50.0, 0xFA } };
            ticks (l, 51.0, 126.0, 24 * 12, 0.0, rnd);
            l[1 + 24 * 4 + 5].ms += 8.0;
            const auto hl = play (l, 5000.0, 1, 0.0f);
            check (errors (hl, 51.0, beat / 2.0, 0.0, 0) < 0.6, "one tick delivered 8 ms late moves the eighths by no more than " + juce::String (errors (hl, 51.0, beat / 2.0, 0.0, 0), 2) + " ms");
        }

        // K. the sender's tempo swept from 120 to 135 over three seconds, half a bpm at a time, with jitter
        {
            std::vector<Ev> w { { 50.0, 0xFA } };
            std::vector<double> eighth;
            double at = 51.0;
            for (int k = 0; k < 24 * 16; ++k)
            {
                if (k % 12 == 0) eighth.push_back (at);
                w.push_back ({ at + (rnd.nextDouble() * 2.0 - 1.0) * 0.5, 0xF8 });
                const double bpm = juce::jlimit (120.0, 135.0, 120.0 + std::floor ((at - 1000.0) / 100.0) * 0.5);
                at += 60000.0 / (24.0 * bpm);
            }
            const auto hw = play (w, at - 40.0, 1, 0.0f);
            double worstW = 0.0;
            for (size_t k = 2; k < hw.on.size() && k < eighth.size(); ++k) worstW = std::max (worstW, std::abs (hw.on[k] - (eighth[k] + lat)));
            check (hw.on.size() + 1 >= eighth.size() && worstW < 2.0 && std::abs (hw.tempo - 135.0) < 0.001,
                   "the sender swept from 120 to 135 over three seconds: the eighths stay within " + juce::String (worstW, 2) + " ms of it all the way; TEMPO ends on " + juce::String (hw.tempo, 1));
        }

        // G. two inputs carrying the same clock: one is listened to
        {
            Engine e;
            e.prepare (48000.0, 128);
            Session ses (e, false);
            Midi m (e, ses, false);
            m.setFollowsClock (true);
            juce::AudioBuffer<float> b (2, 128);
            int portA = 0, portB = 0;
            const double per = 60000.0 / (24.0 * 126.0), dur = 128.0 * 1000.0 / 48000.0;
            double next = 20.0;
            bool first = true;
            for (int k = 0; (double) k * dur < 3000.0; ++k)
            {
                while (next <= (double) k * dur)
                {
                    if (first) { m.clockFrom (&portA, "ONE A", 0xFA, next - 1.0); m.clockFrom (&portB, "ONE B", 0xFA, next - 0.9); first = false; }
                    m.clockFrom (&portA, "ONE A", 0xF8, next); m.clockFrom (&portB, "ONE B", 0xF8, next + 0.2);
                    next += per;
                }
                e.render (b);
            }
            check (e.following() && std::abs (e.followedBpm() - 126.0f) < 0.01f && e.isRunning(), "the same clock on two inputs is counted once (" + juce::String (e.followedBpm(), 1) + " bpm, not twice that)");
            m.setFollowsClock (false);
            for (int k = 0; k < 4; ++k) e.render (b);
            check (! e.following(), "FOLLOW switched off lets go of the clock");
        }
    }

    // 4. the E16: knobs in, values back, presses, the page's lane, travelling rings
    juce::StringArray ringDump;
    {
        Engine e;
        e.prepare (48000.0, 512);
        Session ses (e, false);
        Midi midi (e, ses, false);
        std::vector<juce::MidiMessage> out;
        midi.toE16 = [&] (const juce::MidiMessage& m) { out.push_back (m); };
        juce::AudioBuffer<float> b (2, 512);
        auto audio = [&] (int nb) { for (int i = 0; i < nb; ++i) e.render (b); };
        auto cc = [] (int ch, int num, int v) { return juce::MidiMessage::controllerEvent (ch, num, v); };
        auto ccOut = [&] (int ch, int num) { int v = -1; for (auto& m : out) if (m.isController() && m.getChannel() == ch && m.getControllerNumber() == num) v = m.getControllerValue(); return v; };

        midi.service();
        const int firstGo = (int) out.size();
        for (int k = 0; k < 12; ++k) midi.service();
        std::set<int> told;
        for (auto& m : out) if (m.isController()) told.insert (m.getChannel() * 128 + m.getControllerNumber());
        const int all = e16::kSlots + e16::kLibKnobs + 1;
        check ((int) told.size() == all && (int) out.size() == all && firstGo <= 24 + e16::kLibKnobs + 1,
               "a fresh E16 is told every value once (" + juce::String ((int) out.size()) + " messages: " + juce::String (e16::kSlots) + " knobs, GLIDE and the library scene's "
               + juce::String (e16::kLibKnobs) + " rings), two dozen knobs at a time (" + juce::String (firstGo) + " in the first go)");
        out.clear();

        // a page that comes up is told its sixteen values again, whatever the E16 made of them meanwhile
        midi.incoming (cc (16, 119, 6), true);
        for (int k = 0; k < 4; ++k) midi.service();
        std::set<int> again;
        for (auto& m : out) if (m.isController() && m.getChannel() == 6) again.insert (m.getControllerNumber());
        const bool lanePage = again.size() == 17 && *again.begin() == 12 && *again.rbegin() == 28 && ccOut (16, 30) >= 0 && ses.activeLane == 5;
        out.clear();
        const int laneWas = ses.activeLane;
        midi.incoming (cc (16, 119, 12), true);
        for (int k = 0; k < 4; ++k) midi.service();
        std::set<int> snaps;
        for (auto& m : out) if (m.isController() && m.getChannel() == 16 && m.getControllerNumber() >= 12 && m.getControllerNumber() < 28) snaps.insert (m.getControllerNumber());
        check (lanePage && snaps.size() == 16 && ses.activeLane == laneWas,
               "a page that comes up is told its values again (the bass's sixteen knobs, MID and GLIDE; the snapshot page its sixteen), so a knob's first turn starts where the rig is");
        out.clear();
        midi.incoming (cc (16, 119, 1), true); midi.service(); midi.service(); midi.service(); out.clear();

        // GLIDE from the E16 (knob 1 of a lane's page, pressed), and back to it from the screen
        const float glideWas = e.target (cx::kGlide);
        midi.incoming (cc (16, 30, 64), true);
        midi.service();
        const bool fromE16 = std::abs (e.target (cx::kGlide) - 64.0f / 127.0f) < 1.0e-6f && out.empty();
        ses.userSet (cx::kGlide, glideWas);
        juce::Thread::sleep (160);
        midi.service();
        check (fromE16 && ccOut (16, 30) == e16::to7 (e.control (cx::kGlide), glideWas) && out.size() == 1,
               "GLIDE is turned from the E16 (CC 30 on channel 16) with nothing sent back, and a GLIDE set on the screen is sent to it");
        out.clear();

        // ---- a sequencer's addresses (the ONE's modulation lanes), on a rig of their own
        {
            Engine e3; e3.prepare (48000.0, 512);
            Session s3 (e3, false);
            Midi m3 (e3, s3, false);
            std::vector<juce::MidiMessage> back;
            m3.toE16 = [&back] (const juce::MidiMessage& m) { back.push_back (m); };
            m3.service(); m3.service();
            for (int k = 0; k < 20; ++k) m3.service();          // (a fresh E16 has been told everything)
            back.clear();

            // every control that has an address is reached by it
            int reached = 0; bool right = true;
            for (int ctl = 0; ctl < cx::kCount; ++ctl)
            {
                int ch = 0, num = 0;
                if (! ccx::addressOf (ctl, ch, num)) continue;
                ++reached;
                const int slot = e16::slotFor (ch, num), extra = ccx::extraFor (ch, num);
                right = right && (slot >= 0 ? e16::controlFor (slot) == ctl
                                : extra >= 0 ? (ccx::controlFor (extra, 0) == ctl || ccx::controlFor (extra, 1) == ctl)
                                : (ctl == cx::kGlide && ch == 16 && num == e16::kGlideCc));
            }
            const int expected = kLanes * (kLaneKnobs + 5 + kSlots * kCells + 3) + 4 + 4 * kSpaceModels + 3 + 1 + 16;
            check (right && reached == expected, (juce::String (reached) + " controls have a MIDI address, and each address reaches its own control").toRawUTF8());

            // an insert's places, the returns, the master, a lane's MUTE
            while (e3.insertType (0, 0) != 0) s3.stepInsert (0, 0);     // DELAY in the kick's first slot
            m3.incoming (cc (1, 33, 100), false);                       // its FEEDBACK
            m3.incoming (cc (1, 37, 127), false);                       // its FREEZE (a switch)
            m3.incoming (cc (16, 61, 90), false);                       // DUB FDBK
            m3.incoming (cc (16, 64, 30), false);                       // SPACE SIZE
            m3.incoming (cc (16, 68, 50), false);                       // TAPE
            m3.incoming (cc (3, 56, 127), false);                       // OPEN HAT's MUTE
            m3.service();
            const bool placed = std::abs (e3.target (cx::ins (0, 0, 1)) - 100.0f / 127.0f) < 1.0e-6f && e3.target (cx::ins (0, 0, 5)) > 0.5f
                             && std::abs (e3.target (cx::kRet + 1) - 90.0f / 127.0f) < 1.0e-6f && std::abs (e3.target (cx::space (e3.spaceModel(), 0)) - 30.0f / 127.0f) < 1.0e-6f
                             && std::abs (e3.target (cx::kMaster) - 50.0f / 127.0f) < 1.0e-6f && e3.target (cx::lane (2, cx::kMute)) > 0.5f;
            m3.incoming (cc (1, 37, 0), false); m3.incoming (cc (3, 56, 0), false);
            m3.service();
            check (placed && e3.target (cx::ins (0, 0, 5)) < 0.5f && e3.target (cx::lane (2, cx::kMute)) < 0.5f && back.empty(),
                   "a sequencer reaches an insert's places (CC 32-55 on the lane's channel), the returns and the master (CC 60-70 on channel 16) and a lane's MUTE (CC 56), with nothing sent to the E16 for them");

            // what a sequencer turns, the E16 is told: its knob and ring follow
            m3.incoming (cc (6, 18, 90), false);                        // BASS CUTOFF, from the ONE
            m3.incoming (cc (16, 30, 46), false);                       // GLIDE, from the ONE
            m3.service();
            bool knobTold = false, glideTold = false;
            for (auto& msg : back)
                if (msg.isController())
                {
                    knobTold = knobTold || (msg.getChannel() == 6 && msg.getControllerNumber() == 18 && msg.getControllerValue() == 90);
                    glideTold = glideTold || (msg.getChannel() == 16 && msg.getControllerNumber() == 30 && msg.getControllerValue() == 46);
                }
            check (knobTold && glideTold, "a knob a sequencer turns (and a GLIDE it sets) is sent on to the E16, so the E16's own knob and ring follow");
            back.clear();

            // RECALL: the nth snapshot a snapshot knob holds
            const int cut = cx::lane (5, 6);
            s3.userSet (cx::kGlide, 0.0f);
            s3.userSet (cut, 0.2f); const auto a = s3.take();
            s3.userSet (cut, 0.5f); const auto b = s3.take();
            s3.userSet (cut, 0.8f); const auto c = s3.take();
            s3.finishTakes (true);
            s3.knobs[1].snaps = { a, b, c };
            m3.incoming (cc (16, 81, 2), false); m3.service();
            const bool second = s3.baseId == b && std::abs (e3.target (cut) - 0.5f) < 1.0e-4f && ! e3.isGliding (cut);
            s3.recall (a);
            m3.incoming (cc (16, 81, 2), false); m3.service();          // said again: a lane says its value on every step
            const bool notAgain = s3.baseId == a;
            m3.incoming (cc (16, 81, 0), false); m3.incoming (cc (16, 81, 2), false); m3.service();
            const bool afterNone = s3.baseId == b;
            m3.incoming (cc (16, 81, 9), false); m3.service();          // it holds no ninth
            const bool noNinth = s3.baseId == b;
            m3.incoming (cc (16, 30, 35), false);                       // GLIDE: 1 BAR
            m3.incoming (cc (16, 81, 3), false);
            m3.service();
            check (second && notAgain && afterNone && noNinth && s3.baseId == c && std::abs (glideBarsFor (e3.target (cx::kGlide)) - 1.0) < 1.0e-9
                       && e3.isGliding (cut) && std::abs (e3.target (cut) - 0.8f) < 1.0e-4f,
                   "RECALL (CC 80-95 on channel 16) recalls the nth snapshot a snapshot knob holds: at once with GLIDE off; a value said again does nothing; 0 then the value does; a number past the end does nothing; and a GLIDE sent with it is taken first, so the recall travels by it");

            // ---- what must not wait for the screen's thread, or be lost between two of its visits
            const int mute = cx::lane (2, cx::kMute), freeze = cx::ins (0, 0, 5);
            m3.incoming (cc (3, 56, 127), false);                       // MUTE pressed
            const bool onAtOnce = e3.target (mute) > 0.5f;              // (no visit from the message thread yet)
            m3.incoming (cc (3, 56, 0), false);                         // and let go, before one
            const bool offAtOnce = e3.target (mute) < 0.5f;
            m3.incoming (cc (3, 56, 127), false);                       // and pressed again
            m3.incoming (cc (1, 37, 127), false);                       // the insert's FREEZE held
            const bool heldAtOnce = e3.target (freeze) > 0.5f;
            m3.service();
            check (onAtOnce && offAtOnce && heldAtOnce && e3.target (mute) > 0.5f && e3.target (freeze) > 0.5f,
                   "a key from a sequencer (MUTE, an insert's FREEZE) acts as it arrives, not when the screen's thread next comes round; pressed, let go and pressed again between two of its visits, each is heard");
            m3.incoming (cc (3, 56, 0), false); m3.incoming (cc (1, 37, 0), false); m3.service();
            // GLIDE and then a knob, with no visit between: the knob travels by the new GLIDE
            m3.incoming (cc (16, 30, 0), false);                        // GLIDE: OFF
            m3.incoming (cc (6, 18, 20), false);
            const bool jumps = ! e3.isGliding (cut);
            m3.incoming (cc (16, 30, 64), false);                       // GLIDE: some bars
            m3.incoming (cc (6, 18, 100), false);
            const bool travels = e3.isGliding (cut) && glideBarsFor (e3.target (cx::kGlide)) > 0.0;
            m3.service();
            check (jumps && travels && e3.isGliding (cut), "GLIDE sent just before a knob is taken up before it: the knob travels by it (it went at once, by the GLIDE there was before)");
            // more than the list holds between two visits: the last value of each address still arrives
            for (int i = 0; i < 3000; ++i) m3.incoming (cc (16, 61, i % 100), false);
            m3.incoming (cc (16, 61, 77), false);
            m3.service();
            check (std::abs (e3.target (cx::kRet + 1) - 77.0f / 127.0f) < 1.0e-6f, "three thousand moves of one knob between two visits: the last is where it stands");
        }

        const int tune = cx::lane (0, 0), punch = cx::lane (0, 5);
        midi.incoming (cc (1, 12, 100), true);
        const bool atOnce = std::abs (e.target (tune) - e16::from7 (e.control (tune), 100)) < 1e-6f;
        midi.incoming (cc (1, 17, 90), true);
        midi.service(); audio (2);
        check (atOnce && std::abs (ses.value (punch) - 90.0f / 127.0f) < 1e-6f && out.empty(), "a knob turned on the kick's page moves that knob at once, and nothing is sent back to it");
        ses.turnSnapKnob (0, 0.5f); ses.turnSnapKnob (0, 0.0f);
        check (std::abs (e.target (punch) - 90.0f / 127.0f) < 1e-4f, "the session knows where the hand left it: a snapshot knob there and back returns to it");
        out.clear();

        ses.userSet (cx::lane (2, 5), 0.3f);
        midi.service();
        check (ccOut (3, 17) == 38 && out.size() == 1, "a knob moved on the screen is sent to its place on the E16 (OPEN HAT knob 6: 38)");
        out.clear();

        midi.incoming (cc (5, 27, 127), true); midi.incoming (cc (5, 28, 0), true);
        midi.service();
        check (ses.value (cx::lane (4, cx::kEqHigh)) == 1.0f && ses.value (cx::lane (4, cx::kEqMid)) == 0.0f && ses.value (cx::lane (4, cx::kEqLow)) == 0.5f,
               "knob 16 is HIGH, and MID once it is swapped (its second number)");

        midi.incoming (cc (16, 119, 3), true);
        midi.service();
        check (ses.activeLane == 2, "the E16 turned to page 3 makes OPEN HAT the active lane");
        midi.service(); midi.service();             // (the page is told its values again: not what the next checks count)

        // a snapshot knob from the E16: it pushes the rig, and the pushed knobs' places go back out
        out.clear();
        const int dens = e.find ("closed_hat.density");
        const float d0 = e.target (dens);
        midi.incoming (cc (16, 12, 127), true);
        midi.service(); audio (4); midi.service();
        check (std::abs (ses.value (cx::kSnap) - 1.0f) < 1e-6f && e.target (dens) > d0 + 0.2f && ccOut (2, 21) == e16::to7 (e.control (dens), e.target (dens)) && ccOut (16, 12) < 0,
               "ENERGY turned up on the snapshot page pushes CLOSED HAT DENSITY, and its ring is sent the new place");
        const auto before = ses.baseId;
        out.clear();
        juce::Thread::sleep (170);                                   // (a knob the hand was just on is left alone for 150 ms)
        midi.incoming (cc (16, 44, 127), true); midi.incoming (cc (16, 44, 0), true);
        midi.service(); audio (4); midi.service();
        check (ses.baseId != before && ses.value (cx::kSnap) == 0.0f && ccOut (16, 12) == 0, "its press recalls the knob's nearest snapshot once, and the knob goes home on the E16 too");

        // travelling: GLIDE at 1 bar. A knob turned on the E16 goes there over the bar, and its ring is told.
        ses.userSet (cx::kGlide, 3.0f / 11.0f);
        e.setRunning (true);
        audio (20);
        out.clear();
        {
            const int cut = e.find ("bass.cutoff");
            const float c0 = e.shown (cut);
            midi.incoming (cc (6, 18, c0 < 0.5f ? 120 : 10), true);
            const float want = e.target (cut);
            audio (40); midi.service();
            const float mid = e.shown (cut);
            int reports = 0;
            for (auto& m : out) if (m.isSysEx() && m.getSysExDataSize() >= 8 && m.getSysExData()[3] == 1 && m.getSysExData()[4] == 5 && m.getSysExData()[5] == 18) ++reports;
            audio (200); midi.service();
            check (std::abs (want - c0) > 0.3f && std::abs (mid - c0) > 0.01f && std::abs (mid - want) > 0.01f && reports == 1 && std::abs (e.shown (cut) - want) < 1e-4f && ccOut (6, 18) < 0,
                   "with GLIDE on, a knob turned on the E16 travels there (BASS CUTOFF " + juce::String (c0, 2) + " > " + juce::String (mid, 2) + " > " + juce::String (want, 2) + ") and its ring is told where the sound is");
            out.clear();
        }
        juce::int64 init = 0;
        for (auto& sn : ses.library) if (sn.name == "INIT") init = sn.id;
        ses.recall (init);
        int moves = 0, dones = 0, entries = 0;
        bool wellFormed = true, kept16 = true;
        for (int i = 0; i < 260; ++i)
        {
            audio (1);
            if (i % 3 == 0) midi.service();
            for (auto& m : out)
                if (m.isSysEx())
                {
                    const auto* d = m.getSysExData();
                    const int n = m.getSysExDataSize();
                    wellFormed = wellFormed && n >= 6 && d[0] == 0x7D && d[1] == 0x53 && d[2] == 0x46 && (d[3] == 1 || d[3] == 2) && (n - 4) % (d[3] == 1 ? 4 : 2) == 0
                                 && (n - 4) / (d[3] == 1 ? 4 : 2) <= (d[3] == 1 ? 14 : 28);
                    for (int k = 4; k + 1 < n; k += d[3] == 1 ? 4 : 2) { kept16 = kept16 && d[k + 1] != 27 && d[k + 1] != 28 && d[k] < 11; if (d[3] == 1) ++entries; }
                    (d[3] == 1 ? moves : dones)++;
                    juce::String hex = "f0";
                    for (int k = 0; k < n; ++k) hex << juce::String::toHexString ((int) d[k]).paddedLeft ('0', 2);
                    ringDump.add (hex + "f7");
                }
            out.erase (std::remove_if (out.begin(), out.end(), [] (const juce::MidiMessage& m) { return m.isSysEx(); }), out.end());
        }
        check (wellFormed && kept16 && moves > 5 && dones >= 1 && entries > 20,
               "a recall with GLIDE on reports the travelling knobs to the E16's script and says when each arrives (" + juce::String (moves) + " reports, " + juce::String (dones) + " arrivals)");
    }

    // 5. a recalled pattern waits for the bar line; an edit does not
    {
        auto onsets = [] (bool onBar)
        {
            Engine e;
            e.prepare (48000.0, 512);
            Pattern quarters, offbeats;
            for (int k = 0; k < 16; k += 4) { quarters.steps[(size_t) k].on = true; quarters.steps[(size_t) k].gate = 0.5f; offbeats.steps[(size_t) (k + 2)].on = true; offbeats.steps[(size_t) (k + 2)].gate = 0.5f; }
            for (int k = 0; k < kLanes; ++k) { e.setPattern (k, k == 1 ? quarters : Pattern()); for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (k, sl, -1); }
            e.solo = 1;
            e.set (cx::lane (1, cx::kSendA), 0.0f); e.set (cx::lane (1, cx::kSendB), 0.0f);       // no dub delay: the steps alone
            e.setRunning (true);
            juce::AudioBuffer<float> b (2, 512);
            std::vector<double> beats;                                   // where each hit starts, in beats
            const double spb = 48000.0 * 60.0 / e.tempo();
            int quiet = 100000;
            for (int done = 0; done < (int) (spb * 12.5); done += 512)
            {
                if (done <= (int) (spb * 5.3) && done + 512 > (int) (spb * 5.3)) e.setPattern (1, offbeats, onBar);     // in bar 2, after its second beat
                e.render (b);
                for (int i = 0; i < 512; ++i)
                {
                    const bool loud = std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i)) > 4e-3f;
                    if (loud && quiet > 2400) beats.push_back ((double) (done + i) / spb);
                    quiet = loud ? 0 : quiet + 1;
                }
            }
            return beats;
        };
        auto shape = [] (const std::vector<double>& beats) { juce::String o; const double lat = beats.empty() ? 0.0 : beats[0]; for (auto q : beats) o << juce::String (q - lat, 1) << " "; return o.trim(); };
        const auto waited = shape (onsets (true)), atOnce = shape (onsets (false));
        check (waited == "0.0 1.0 2.0 3.0 4.0 5.0 6.0 7.0 8.5 9.5 10.5 11.5", "a recalled pattern changes on the bar line: quarters to the end of bar 2, then offbeats (" + waited + ")");
        check (atOnce == "0.0 1.0 2.0 3.0 4.0 5.0 5.5 6.5 7.5 8.5 9.5 10.5 11.5", "an edited pattern changes at once (" + atOnce + ")");
    }

    // 5b. the library's scene: the grid is the map, a turn is heard in the headphones, a press recalls
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_lib_test");
        tmp.deleteRecursively();
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        {
            Engine e;
            e.prepare (48000.0, 256);
            Session ses (e, true);
            Midi m (e, ses, false);
            std::vector<juce::MidiMessage> out;
            std::map<std::pair<int, int>, int> knobAt;                  // the E16's own knobs: where each stands (it moves them when told a value)
            m.toE16 = [&] (const juce::MidiMessage& msg) { out.push_back (msg); if (msg.isController()) knobAt[{ msg.getChannel(), msg.getControllerNumber() }] = msg.getControllerValue(); };
            juce::AudioBuffer<float> b (2, 256);
            auto bars = [&] (double n) { const int blocks = (int) (n * 4.0 * 60.0 / e.tempo() * 48000.0 / 256.0); for (int k = 0; k < blocks; ++k) e.render (b); };
            auto state = [&] (std::initializer_list<int> keep)
            {
                for (int l = 0; l < kLanes; ++l) ses.userSet (cx::lane (l, cx::kMute), std::find (keep.begin(), keep.end(), l) != keep.end() ? 0.0f : 1.0f);
                bars (6.2);
            };
            auto take = [&] (std::initializer_list<int> keep) { state (keep); juce::Thread::sleep (3); return ses.take(); };
            for (int k = 0; k < 200; ++k) e.render (b);
            e.setRunning (true);
            const auto all = take ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }), kick = take ({ 0 }), hats = take ({ 1, 2 }), pads = take ({ 9, 10 }), drums = take ({ 0, 1, 2, 3, 4 });
            ses.waitForClips();
            auto prof = [&] (juce::int64 id) { return ses.library[(size_t) ses.indexOf (id)].profile; };
            auto knobOf = [&] (juce::int64 id) { const auto p = prof (id); return (3 - Session::energyRow (p)) * 4 + Session::brightCol (p); };
            // a knob of the E16 turned so many clicks from where it stands (it stops at its ends)
            auto turn = [&] (int ch, int knob, int clicks)
            {
                auto& at = knobAt[{ ch, e16::kKnobCc + knob }];
                const int step = clicks > 0 ? 1 : -1;
                for (int k = 0; k != clicks; k += step)
                {
                    if (at + step < 0 || at + step > 127) break;
                    at += step;
                    m.incoming (juce::MidiMessage::controllerEvent (ch, e16::kKnobCc + knob, at), true);
                }
                m.service();
            };
            auto press = [&] (int ch, int knob) { m.incoming (juce::MidiMessage::controllerEvent (ch, e16::kPressCc + knob, 127), true); m.service(); };
            auto perStep = [] (int n) { return n > 1 ? juce::jlimit (1, 4, 119 / (n - 1)) : 4; };
            m.service();                                                // (a fresh E16 is told where every knob stands)

            // the map: each snapshot is under the knob for its energy and brightness
            bool placed = knobOf (all) < 8 && knobOf (hats) % 4 == 3 && knobOf (kick) % 4 == 0 && knobOf (pads) >= 8 && knobOf (all) != knobOf (kick) && knobOf (all) != knobOf (hats);
            const auto cell = ses.mapCell (Session::energyRow (prof (hats)), Session::brightCol (prof (hats)));
            const int where = (int) (std::find (cell.begin(), cell.end(), hats) - cell.begin());
            turn (e16::kMapChannel, knobOf (hats), 1);                  // a first click: the first it holds
            const bool first = ses.cueing() == cell.front();
            turn (e16::kMapChannel, knobOf (hats), where * perStep ((int) cell.size()));
            const bool heard = first && ses.cueing() == hats && ses.baseId != hats;
            for (int k = 0; k < 8; ++k) e.render (b);
            const bool sounding = e.cueBlock().getMagnitude (0, 256) > 0.01f;

            // PHONES: one pair of outputs. While a cue plays, the set is in the left ear and the cue in the right.
            juce::AudioBuffer<float> o (2, 256);
            auto outputs = [&] { e.render (b); o.makeCopyOf (b); e.phonesSplit (o.getWritePointer (0), o.getWritePointer (1), 256); };
            auto asTheSet = [&] { bool is = true; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) is = is && o.getSample (ch, i) == b.getSample (ch, i); return is; };
            outputs();
            const bool offLeavesIt = asTheSet();
            e.setPhones (true);
            for (int k = 0; k < 6; ++k) outputs();                     // (it comes in over 10 ms)
            bool split = b.getMagnitude (0, 256) > 0.01f && e.cueBlock().getMagnitude (0, 256) > 0.01f;
            for (int i = 0; i < 256; ++i)
                split = split && std::abs (o.getSample (0, i) - 0.5f * (b.getSample (0, i) + b.getSample (1, i))) < 1.0e-6f
                              && std::abs (o.getSample (1, i) - 0.5f * (e.cueBlock().getSample (0, i) + e.cueBlock().getSample (1, i))) < 1.0e-6f;

            press (e16::kMapChannel, knobOf (hats));
            const bool recalled = ses.baseId == hats && ses.cueing() == 0;
            for (int k = 0; k < 12; ++k) outputs();                    // the cue fades away, and the split with it
            check (offLeavesIt && split && asTheSet(),
                   "PHONES: with it off the outputs are the set; lit, while a cue plays the left output is the set (both its sides) and the right is the cue; when the cue has gone the outputs are the set again");
            e.setPhones (false);
            check (placed && heard && sounding && recalled,
                   "the map: the whole rig sits in the upper rows, the hats at the bright edge, the kick at the dark edge, pads low (knobs "
                       + juce::String (knobOf (all) + 1) + ", " + juce::String (knobOf (hats) + 1) + ", " + juce::String (knobOf (kick) + 1) + ", " + juce::String (knobOf (pads) + 1)
                       + "); turning a knob plays its snapshot in the headphones and leaves the set alone; pressing it recalls it");

            // an empty square does nothing; a ring shows where its knob is among what it holds
            int empty = -1;
            for (int k = 0; k < 16 && empty < 0; ++k) if (ses.mapCell (3 - k / 4, k % 4).empty()) empty = k;
            const auto baseWas = ses.baseId;
            if (empty >= 0) { turn (e16::kMapChannel, empty, 40); press (e16::kMapChannel, empty); }
            const bool nothing = empty < 0 || (ses.cueing() == 0 && ses.baseId == baseWas);
            juce::Thread::sleep (170);
            for (int k = 0; k < 21; ++k) m.service();
            int kickRing = -1, emptyRing = -1;
            for (auto& msg : out)
                if (msg.isController() && msg.getChannel() == e16::kMapChannel)
                {
                    if (msg.getControllerNumber() == e16::kKnobCc + knobOf (kick)) kickRing = msg.getControllerValue();
                    if (empty >= 0 && msg.getControllerNumber() == e16::kKnobCc + empty) emptyRing = msg.getControllerValue();
                }
            const int kickN = (int) ses.mapCell (Session::energyRow (prof (kick)), Session::brightCol (prof (kick))).size();
            check (nothing && kickRing == (kickN == 1 ? 64 : 8) && (empty < 0 || emptyRing == 0),
                   "an empty square's knob does nothing and its ring is sent to nought; a knob not yet turned shows the first of what it holds (its ring at " + juce::String (kickRing) + ", holding " + juce::String (kickN) + ")");

            // the finders: RECENT newest first; MORE and LESS a step up and down from what is playing
            turn (e16::kFindChannel, 0, 1);
            const bool newest = ses.cueing() == drums;
            turn (e16::kFindChannel, 0, 127);
            const bool oldest = ses.cueing() == all;

            // The knob stays on its snapshot. Its ring is put where that snapshot stands among the
            // others, and that is all that moves: turned slowly afterwards, a click at a time with
            // the ring put back between clicks, it still gets to the next one.
            const int recentN = (int) ses.recent (32).size(), per = perStep (recentN);
            // (the audio runs on meanwhile, as it does in life: a clip the headphones have left is let go of)
            auto rest = [&] { for (int k = 0; k < 8; ++k) e.render (b); juce::Thread::sleep (170); m.service(); for (int k = 0; k < 8; ++k) e.render (b); };
            turn (e16::kFindChannel, 0, -127);                          // back to the newest
            rest();
            turn (e16::kFindChannel, 0, per);                           // the second
            const auto second = ses.cueing();
            rest();                                                     // the hand is off it: its ring is put in the second's place
            const int ringAt = knobAt[{ e16::kFindChannel, e16::kKnobCc }];
            for (int k = 0; k < 30; ++k) m.service();
            const bool stays = ses.cueing() == second && second != drums && second != 0 && ringAt == 8 + (119 + (recentN - 1) / 2) / std::max (1, recentN - 1);
            bool early = false;
            for (int k = 0; k < per; ++k)
            {
                turn (e16::kFindChannel, 0, 1);
                if (k < per - 1) early = early || ses.cueing() != second;
                rest();                                                 // (between clicks the ring is put back in place)
            }
            const auto third = ses.cueing();
            check (stays && ! early && third != second && third != drums && third != 0,
                   ("a knob stays on the snapshot it was turned to: its ring is put in that snapshot's place (" + juce::String (ringAt) + " for the second of " + juce::String (recentN)
                       + ") and nothing else moves; turned on a click at a time, with the ring put back between clicks, the next snapshot comes after " + juce::String (per) + " clicks").toRawUTF8());
            turn (e16::kFindChannel, 0, -127);
            rest();
            state ({ 0 });                                              // the kick alone playing now
            juce::Thread::sleep (1600);                                 // (what is playing is profiled every second and a half)
            const auto now = ses.heardNow();
            turn (e16::kFindChannel, 2, 1);
            const auto up = ses.cueing();
            turn (e16::kFindChannel, 3, 1);
            const auto down = ses.cueing();
            turn (e16::kFindChannel, 1, 1);
            const auto like = ses.cueing();
            const bool steps = up != 0 && prof (up).energy > now.energy + 0.03f && down != 0 && prof (down).energy < now.energy - 0.03f && like == kick
                               && prof (up).energy <= prof (all).energy && up != all;       // (the nearest step up, not the furthest)
            press (e16::kFindChannel, 2);
            check (newest && oldest && steps && ses.baseId == up,
                   "RECENT runs from the newest to the oldest; with the kick alone playing (energy " + juce::String (now.energy, 2) + "), FITS NOW finds the kick alone, MORE a snapshot with more energy ("
                       + juce::String (up != 0 ? prof (up).energy : 0.0f, 2) + "), LESS one with less (" + juce::String (down != 0 ? prof (down).energy : 0.0f, 2) + "); pressing MORE recalls it");
        }
        // the scene file
        const auto lib = juce::JSON::parse (e16::libraryText());
        bool form = lib.isObject() && lib["title"].toString() == "SF-LIB" && lib["pages"].isArray() && lib["pages"].size() == 12;
        if (form)
        {
            const auto map = lib["pages"][0], find = lib["pages"][1];
            form = map["title"].toString() == "MAP" && find["title"].toString() == "FIND" && map["encoders"].size() == 16;
            for (int p = 0; p < 16 && form; ++p)
            {
                const auto en = map["encoders"][p];
                form = (int) en["turn_actions"][0]["channel"] == e16::kMapChannel && (int) en["turn_actions"][0]["nr1"] == e16::kKnobCc + p
                       && (int) en["push_action"]["channel"] == e16::kMapChannel && (int) en["push_action"]["nr1"] == e16::kPressCc + p && en["name"].toString().length() <= 8;
            }
            form = form && find["encoders"][0]["name"].toString() == "RECENT" && (int) find["encoders"][2]["turn_actions"][0]["channel"] == e16::kFindChannel
                   && (int) find["encoders"][5]["turn_actions"][0]["type"] == 0 && lib["pages"][2]["encoders"].size() == 16;
        }
        check (form, "the library's scene is written: MAP on channel 15, FIND on 14, knob p on CC 11 + p with its press on CC 43 + p, ten empty pages");
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
    }

    // 6. the scene, and the session's backups
    {
        Engine e;
        juce::StringArray names;
        for (auto* n : { "ENERGY", "BUILD", "ACID", "DIST", "WARP" }) names.add (n);
        while (names.size() < 16) names.add ({});
        const auto text = e16::sceneText (e, names);
        const juce::var v = juce::JSON::parse (text);
        const auto* pages = v.getProperty ("pages", juce::var()).getArray();
        bool ok = pages != nullptr && pages->size() == 12 && v.getProperty ("title", "").toString() == "SF-000";
        int knobs = 0;
        if (ok)
            for (auto& pg : *pages)
            {
                const auto* encs = pg.getProperty ("encoders", juce::var()).getArray();
                ok = ok && encs != nullptr && encs->size() == 16;
                if (ok) for (auto& en : *encs) { const auto nm = en.getProperty ("name", "").toString(); ok = ok && nm.isNotEmpty() && nm.length() <= 8 && en.getProperty ("abbr", "").toString().length() <= 4; ++knobs; }
            }
        const auto code = v.getProperty ("code", juce::var()).getProperty ("code", "").toString();
        ok = ok && code.length() > 500 && code.length() < 7000 && ! code.contains ("@") && ! code.dropLastCharacters (1).contains ("\n");
        check (ok && knobs == 192, "the E16 scene is written: 12 pages of 16, names that fit, the script inside (" + juce::String (text.length()) + " bytes)");
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_midi_test");
        tmp.deleteRecursively(); tmp.createDirectory();
        tmp.getChildFile ("SF-000.oxie16").replaceWithData (text.toRawUTF8(), text.getNumBytesAsUTF8());
        tmp.getChildFile ("rings.txt").replaceWithText (ringDump.joinIntoString ("\n"));

        Session::stateFileForTests = tmp.getChildFile ("session.json");
        for (int k = 0; k < 3; ++k)
        {
            Engine e2;
            Session s2 (e2, true);
            s2.userSet (cx::lane (0, 1), 0.1f * (float) (k + 1));
            s2.save();
            juce::Thread::sleep (1100);                                  // backups are named to the second
        }
        const int kept = tmp.getChildFile ("backups").getNumberOfChildFiles (juce::File::findFiles, "session-*.json");
        Session::stateFileForTests = juce::File();
        check (kept == 2, "each time the app opens, the session as it found it is copied aside (" + juce::String (kept) + " copies after three launches, the first with nothing to copy)");
        std::printf ("  the scene and the ring reports are in %s (tests/e16_scene_test.py reads them)\n", tmp.getFullPathName().toRawUTF8());
    }
    std::printf (bad == 0 ? "ALL MIDI CHECKS PASSED\n" : "%d MIDI CHECK(S) FAILED\n", bad);
    return bad;
}

// Where the time goes: the engine's share of real time (by lane, inserts, returns, master) at three
// buffer sizes, and what one frame of the screen costs. Offline, on whatever machine runs it.
static int perf()
{
    using namespace sf0;
    std::printf ("SF-000 load (this machine, one core; real time = 100 %%)\n");
    for (int block : { 64, 128, 512 })
    {
        Engine e;
        e.prepare (48000.0, block);
        e.setRunning (true);
        juce::AudioBuffer<float> b (2, block);
        for (int i = 0; i < 48000 * 2 / block; ++i) e.render (b);          // settle
        e.profile = true;
        const double secs = 16.0;
        for (int i = 0; i < (int) (48000.0 * secs / block); ++i) e.render (b);
        double lanesSum = 0.0, insSum = 0.0;
        for (int l = 0; l < kLanes; ++l) { lanesSum += e.profLane[(size_t) l]; insSum += e.profIns[(size_t) l]; }
        std::printf ("  blocks of %3d: engine %5.1f %%  (instruments %.1f, inserts %.1f, returns %.1f, master %.1f, the rest %.1f)\n", block, 100.0 * e.profAll / secs,
                     100.0 * lanesSum / secs, 100.0 * insSum / secs, 100.0 * e.profRet / secs, 100.0 * e.profMaster / secs,
                     100.0 * (e.profAll - lanesSum - insSum - e.profRet - e.profMaster) / secs);
        if (block == 128)
        {
            std::printf ("    by lane:");
            for (int l = 0; l < kLanes; ++l) std::printf (" %s %.1f", lanes()[(size_t) l].slug, 100.0 * e.profLane[(size_t) l] / secs);
            std::printf ("\n");
        }
    }
    {
        Engine e;
        e.prepare (48000.0, 128);
        Session ses (e, false);
        e.setRunning (true);
        Screen sc (ses);
        sc.setSize ((int) Screen::kW, (int) Screen::kH);
        juce::AudioBuffer<float> b (2, 128);
        for (int i = 0; i < 400; ++i) e.render (b);
        juce::Image img (juce::Image::ARGB, (int) Screen::kW * 2, (int) Screen::kH * 2, true);
        auto frame = [&] (juce::Rectangle<int> clip, int times)
        {
            juce::int64 spent = 0;
            for (int i = 0; i < times; ++i)
            {
                e.render (b);                                           // (the meters move; not timed)
                const auto t0 = juce::Time::getHighResolutionTicks();
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                g.reduceClipRegion (clip);
                sc.paintEntireComponent (g, true);
                spent += juce::Time::getHighResolutionTicks() - t0;
            }
            return 1000.0 * juce::Time::highResolutionTicksToSeconds (spent) / times;
        };
        const double first = frame (sc.getLocalBounds(), 1);          // (lit: this one makes the pictures the rest are put together from)
        const double whole = frame (sc.getLocalBounds(), 30), master = frame ({ 1151, 782, 554, 190 }, 200), knob = frame ({ 300, 150, 76, 56 }, 200),
                     none = frame ({ 0, 0, 1, 1 }, 200);
        std::printf ("  the screen (drawn here in software at twice the size): the whole of it %.2f ms, the meters %.2f ms, one knob %.2f ms, nothing %.3f ms; the first time, %.0f ms\n", whole, master, knob, none, first);
        sc.showConfig ({ 0, 7, 0 });
        const double cfgFirst = frame (sc.getLocalBounds(), 1);
        sc.showConfig ({ 0, 7, 0 });                                  // (shut again)
        std::printf ("    CONFIG, the first time it opens (its plate is made then): %.0f ms\n", cfgFirst);
        sc.setLit (false);
        const double fWhole = frame (sc.getLocalBounds(), 30), fMaster = frame ({ 1151, 782, 554, 190 }, 200), fKnob = frame ({ 300, 150, 76, 56 }, 200);
        std::printf ("  the same with the flat drawing (L on the keyboard): the whole of it %.2f ms, the meters %.2f ms, one knob %.2f ms\n", fWhole, fMaster, fKnob);
    }
    return 0;
}

// The 3D stage, played: SF-000 --stagetest. It needs a display to open a window on (xvfb will do)
// and OpenGL 3.2 (a software driver will do: this is about what is drawn and what the hand
// reaches, not how fast).
// The 3D stage is started with a note left beside the session, and the note is taken away once
// the stage is up and has drawn (or has said why it is not, or the app has left in good order).
// A note still there at launch means the last start never finished: the app died, or was
// killed, while the stage was coming up. 3D is then left off for that launch, and says so, so
// that a stage which takes the app down cannot take it down at every launch. (JUCE on the Mac
// walks into an OpenGL context it failed to make, before anything of SF-000's can answer.)
struct StageGuard
{
    juce::File note;
    bool lastStartDied() const  { return note.existsAsFile(); }
    void starting() const       { note.getParentDirectory().createDirectory(); note.replaceWithText (juce::Time::getCurrentTime().toString (true, true)); }
    void settled() const        { note.deleteFile(); }
};

static int stageTest()
{
    using namespace sf0;
    int bad = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++bad; };
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    std::printf ("SF-000 3D stage test\n");
    Engine e;
    e.prepare (48000.0, 512);
    Session ses (e, false);
    juce::AudioBuffer<float> b (2, 512);
    auto audio = [&] (int blocks) { for (int i = 0; i < blocks; ++i) e.render (b); };
    audio (40);
    {
        // 0. where it cannot start, the lit skin stays and says why
        setenv ("SF000_3D_FAIL", "1", 1);
        Screen sc (ses);
        sc.setSize (864, 558);
        sc.addToDesktop (0); sc.setVisible (true);
        int told = 0;
        sc.onLook = [&told] { ++told; };
        sc.setThree (true);
        for (int i = 0; i < 200 && sc.isThree(); ++i) pump (25);
        const auto img = sc.createComponentSnapshot (sc.getLocalBounds(), false, 0.5f);
        check (! sc.isThree() && ! sc.threeShowing() && sc.threeNote().isNotEmpty() && sc.isLit() && told == 1, "a stage that cannot start leaves the lit skin up, says why (" + sc.threeNote() + ") and has the look kept");
        check (img.getPixelAt (300, 200).getBrightness() > 0.2f, "and the lit skin is drawn as before");
        unsetenv ("SF000_3D_FAIL");
        sc.removeFromDesktop();
    }
    {
        // 0b. where it starts and then draws nothing (one maker's chips blanking the frame), it notices, gives up and says so
        setenv ("SF000_3D_FAIL", "blank", 1);
        Screen sc (ses);
        sc.setSize (864, 558);
        sc.addToDesktop (0); sc.setVisible (true);
        int told = 0;
        sc.onLook = [&told] { ++told; };
        sc.setThree (true);
        bool wasUp = false;
        for (int i = 0; i < 400 && sc.isThree(); ++i) { wasUp = wasUp || sc.threeShowing(); pump (25); }
        const auto img = sc.createComponentSnapshot (sc.getLocalBounds(), false, 0.5f);
        check (wasUp && ! sc.isThree() && ! sc.threeShowing() && sc.threeNote().contains ("blank") && sc.isLit() && told == 1, "a stage that starts and then draws a blank picture is caught, and the lit skin comes back (" + sc.threeNote() + ")");
        check (img.getPixelAt (300, 200).getBrightness() > 0.2f, "drawn as before");
        unsetenv ("SF000_3D_FAIL");
        sc.removeFromDesktop();
    }
    {
        // 0c. a context that is made and never draws: six seconds in front of the player, and the lit skin is back
        setenv ("SF000_3D_FAIL", "stall", 1);
        Screen sc (ses);
        sc.setSize (864, 558);
        sc.addToDesktop (0); sc.setVisible (true);
        int began = 0, ended = 0;
        sc.onThreeStarting = [&began] { ++began; };
        sc.onThreeSettled = [&ended] { ++ended; };
        sc.setThree (true);
        pump (500);
        const bool waiting = sc.isThree() && ! sc.threeShowing() && began == 1 && ended == 0;
        const auto now = juce::Time::getMillisecondCounter();
        sc.testWatch (now, true);
        const bool notYet = ! sc.testWatch (now + 3000, true) && ! sc.testWatch (now + 3500, false);      // (out of sight, the count starts over)
        sc.testWatch (now + 4000, true);
        const bool stillNot = ! sc.testWatch (now + 9000, true);
        const bool gaveUp = sc.testWatch (now + 10500, true);
        check (waiting && notYet && stillNot && gaveUp && ! sc.isThree() && sc.threeNote().contains ("six seconds") && ended == 1,
               "a stage that is made and never draws is given six seconds in front of the player, then the lit skin comes back (" + sc.threeNote() + ")");
        unsetenv ("SF000_3D_FAIL");
        sc.removeFromDesktop();
        // the note left while a stage starts: still there at the next launch if the app died meanwhile
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_stage_guard");
        tmp.deleteRecursively();
        const StageGuard g { tmp.getChildFile ("3d-starting") };
        const bool clean = ! g.lastStartDied();
        g.starting();
        const bool died = StageGuard { tmp.getChildFile ("3d-starting") }.lastStartDied();
        g.settled();
        check (clean && died && ! g.lastStartDied(), "a start that never finished leaves its note for the next launch to find; one that did takes it away");
        tmp.deleteRecursively();
    }
    Screen sc (ses);
    sc.setSize (1296, 838);
    sc.addToDesktop (0); sc.setVisible (true);
    int began = 0, ended = 0;
    sc.onThreeStarting = [&began] { ++began; };
    sc.onThreeSettled = [&ended] { ++ended; };
    sc.setThree (true);
    for (int i = 0; i < 400 && sc.isThree() && ! sc.threeShowing(); ++i) pump (25);
    check (sc.threeShowing(), "the 3D stage starts" + (sc.threeShowing() ? " (" + sc.stageOrNull()->driver() + ")" : ": " + sc.threeNote()));
    if (! sc.threeShowing()) { std::printf ("%d FAILED\n", bad); return 1; }
    auto* st = sc.stageOrNull();
    for (int i = 0; i < 200 && sc.isThree() && st->firstPictureSpread() < 0; ++i) pump (25);
    check (sc.isThree() && st->firstPictureSpread() >= 6, "and its first picture is looked at and is a picture (darkest to brightest: " + juce::String (st->firstPictureSpread()) + " of 255)");
    // everything brought up to date and a frame drawn (which is also when the eye's place is known to the hand)
    auto fresh = [&] { sc.testTick(); sc.refreshNow(); st->settle(); return st->capture(); };
    auto view = [&] (int v)
    {
        Screen::Eye eye = sc.eye();
        eye.pose = Stage::Pose(); eye.pose.view = v; eye.pose.turn = v == 2 ? -30.0f : 0.0f; eye.pose.tilt = v == 0 ? 89.9f : v == 1 ? 56.0f : 31.0f;
        sc.setEye (eye);
        fresh();
    };
    auto pt = [&] (juce::Point<float> u) { return st->pointsOf (u.x, u.y, st->heightAt (u.x, u.y)); };
    auto ev = [&] (juce::Point<float> p, juce::Point<float> down, bool dragged, juce::ModifierKeys mods = juce::ModifierKeys())
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &sc, &sc, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
    };
    auto click = [&] (juce::Point<float> u) { const auto p = pt (u); sc.mouseDown (ev (p, p, false)); sc.mouseUp (ev (p, p, false)); fresh(); };
    auto dragPoints = [&] (juce::Point<float> u, float dx, float dy)
    {
        const auto p = pt (u);
        sc.mouseDown (ev (p, p, false));
        for (int k = 1; k <= 4; ++k) sc.mouseDrag (ev (p + juce::Point<float> (dx, dy) * ((float) k / 4.0f), p, true));
        sc.mouseUp (ev (p + juce::Point<float> (dx, dy), p, true));
        fresh();
    };
    auto knobTags = [&] { std::vector<int> v; for (int t : sc.testTags()) if (t >= 0) v.push_back (t); return v; };      // (not INFO's box or the line of words)
    // a knob moved as the E16 moves one (1) or as a sequencer's port does (2): the engine first, then who it was, then the session's record
    auto fromOutside = [&] (int ctl, float v, int by) { e.set (ctl, v, 0.0); ses.moved (ctl, by); ses.userSet (ctl, v, true); audio (4); };
    auto find = [&] (const std::function<bool (const Screen::TestHit&)>& is) { for (auto& h : sc.testHits()) if (is (h)) return h.r.getCentre(); return juce::Point<float> (-1.0f, -1.0f); };

    // 1. what the hand can reach, through the eye: each control's place on the window is that control
    for (int v = 0; v < 3; ++v)
    {
        view (v);
        int all = 0, back = 0, knobs = 0, knobsBack = 0;
        for (auto& h : sc.testHits())
        {
            const auto u = h.r.getCentre();
            const auto got = st->unitsAt (pt (u));
            const bool ok = h.r.expanded (1.0f).contains (got);
            ++all; back += ok ? 1 : 0;
            if (Screen::testIsKnob (h)) { ++knobs; knobsBack += ok ? 1 : 0; }
        }
        static const char* const names[] = { "from above", "the player's view", "from the corner" };
        // (from the corner a knob's cap hides some of what is behind it, as it would on the bench: the knobs themselves are all in reach)
        const bool ok = v < 2 ? back >= all - all / 100 : knobsBack >= knobs - knobs / 50;
        check (all > 300 && ok, juce::String (names[v]) + ": " + juce::String (back) + " of " + juce::String (all) + " controls answer where they are drawn (" + juce::String (knobsBack) + " of " + juce::String (knobs) + " knobs)");
    }

    // 2. the hand, in the player's view
    view (1);
    auto stepKey = [&] (int index) { return find ([index] (const Screen::TestHit& h) { return Screen::testIsStep (h) && h.a == index; }); };
    click (stepKey (1));
    check (ses.pattern (7).steps[1].on && ses.selStep == 1, "a tap on a step key lights its step");
    click (stepKey (1));
    check (! ses.pattern (7).steps[1].on, "and another puts it out");
    {
        const float before = ses.pattern (7).steps[9].on ? ses.pattern (7).steps[9].vel : 0.75f;
        const auto u = stepKey (9);
        const auto p0 = pt (u), p1 = st->pointsOf (u.x, u.y - 24.0f, st->heightAt (u.x, u.y));
        dragPoints (u, p1.x - p0.x, p1.y - p0.y);
        check (ses.pattern (7).steps[9].on && ses.pattern (7).steps[9].vel > before + 0.2f, "a step dragged up the instrument gets louder (" + juce::String (before, 2) + " to " + juce::String (ses.pattern (7).steps[9].vel, 2) + ")");
    }
    {
        const int mute = cx::lane (2, cx::kMute), solo = cx::lane (4, cx::kSolo);
        click (find ([mute] (const Screen::TestHit& h) { return Screen::testIsToggle (h, mute); }));
        check (ses.value (mute) > 0.5f, "a lane's M key mutes it");
        click (find ([mute] (const Screen::TestHit& h) { return Screen::testIsToggle (h, mute); }));
        check (ses.value (mute) < 0.5f, "and unmutes it");
        click (find ([solo] (const Screen::TestHit& h) { return Screen::testIsToggle (h, solo); }));
        check (ses.value (solo) > 0.5f, "a lane's S key solos it");
        click (find ([solo] (const Screen::TestHit& h) { return Screen::testIsToggle (h, solo); }));
    }
    {
        const int ctl = cx::lane (0, 5);
        ses.userSet (ctl, 0.2f); audio (4); fresh();
        const auto u = find ([ctl] (const Screen::TestHit& h) { return Screen::testIsControl (h, ctl); });
        const float perUnit = std::min (1296.0f / Screen::kW, 838.0f / Screen::kH);
        dragPoints (u, 0.0f, -48.0f * perUnit);
        check (std::abs (ses.value (ctl) - (0.2f + 48.0f / 160.0f)) < 0.01f, "a knob dragged up turns as far as on the flat screen, however near the eye is (" + juce::String (ses.value (ctl), 3) + ")");
        check (knobTags().empty(), "let go, its tag is gone");
        const auto p = pt (u);
        sc.mouseDown (ev (p, p, false)); sc.mouseDrag (ev (p + juce::Point<float> (0.0f, -6.0f), p, true)); sc.testTick();
        const auto tags = knobTags();
        check (tags.size() == 1 && tags[0] == ctl && sc.testTagText (ctl).startsWith ("PUNCH|") && sc.testFollowing().isEmpty(), "in the hand, its tag is up with its name and reading (" + sc.testTagText (ctl) + "), and the eye stays where it is");
        sc.mouseUp (ev (p, p, true)); fresh();
    }
    {
        const bool was = e.isRunning();
        click (find (Screen::testIsRun));
        check (e.isRunning() != was, "the RUN key starts and stops it");
        click (find (Screen::testIsRun));
    }
    {
        click (find ([] (const Screen::TestHit& h) { return Screen::testIsConfigKey (h, 2); }));
        for (int i = 0; i < 12; ++i) fresh();
        check (! sc.testConfigRect().isEmpty(), "a lane's C key opens CONFIG");
        // its knobs are in reach where they are drawn, and the rows cut off by its panel are not
        int cfgKnobs = 0, cfgBack = 0;
        for (auto& h : sc.testHits()) if (Screen::testIsConfig (h)) { ++cfgKnobs; cfgBack += h.r.expanded (1.0f).contains (st->unitsAt (pt (h.r.getCentre()))) ? 1 : 0; }
        check (cfgKnobs > 0 && cfgBack == cfgKnobs, "CONFIG's own controls answer where they are drawn (" + juce::String (cfgBack) + " of " + juce::String (cfgKnobs) + ")");
        sc.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        for (int i = 0; i < 12; ++i) fresh();
    }
    {
        click (find (Screen::testIsLibraryKey));
        check (sc.testPage() == Screen::pLibrary, "the LIBRARY key turns to the library");
        const auto img = fresh();
        check (img.isValid(), "which is drawn on the stage too");
        sc.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); fresh();
        check (sc.testPage() == Screen::pMain, "and Esc turns back");
    }
    {
        // a name is typed in a small window of its own, over the stage
        sc.setPage (Screen::pMacros); fresh();
        sc.testRenameKnob(); pump (50);
        check (sc.testEditing() && sc.testEditorFloats(), "renaming a snapshot knob opens a box over the stage");
        sc.testEndEdit ("dawn"); fresh();
        check (! sc.testEditing() && ses.knobs[0].name == "DAWN", "and what is typed is taken (" + ses.knobs[0].name + ")");
        sc.setPage (Screen::pMain); fresh();
    }

    // 3. the sheet: a part of it printed again is the same as all of it printed again
    {
        ses.userSet (cx::kTempo, 0.37f); ses.userSet (cx::lane (3, 2), 0.9f, true); audio (30);
        const auto part = fresh();
        sc.repaint();
        const auto whole = fresh();
        int differ = 0;
        if (part.isValid() && whole.isValid() && part.getBounds() == whole.getBounds())
        {
            const juce::Image::BitmapData a (part, juce::Image::BitmapData::readOnly), c (whole, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < a.height; ++y) if (std::memcmp (a.getLinePointer (y), c.getLinePointer (y), (size_t) a.width * 4) != 0) ++differ;
        }
        else differ = -1;
        check (differ == 0, "a part of the sheet printed again gives the picture a whole printing gives (" + juce::String (differ) + " rows differ)");
        check (began == 1 && ended == 1, "the stage said it was starting, and that it was up once it had drawn");

        // 3b. nothing is built again that has not changed
        fresh();
        const int b0 = st->builds();
        sc.repaint(); fresh();                                          // all of the sheet printed again: no part differs
        ses.userSet (cx::kMaster, 0.31f); audio (20); fresh(); fresh(); // the meters move
        const int b1 = st->builds();
        const auto before = st->sheet().createCopy();
        const auto picBefore = fresh();
        for (int l = 0; l < 6; ++l) for (int k = 0; k < 12; ++k) { e.set (cx::lane (l, k), 0.83f); ses.moved (cx::lane (l, k), 2); ses.userSet (cx::lane (l, k), 0.83f, true); }      // 72 knobs, as a sequencer or a snapshot moves them
        audio (30); fresh();
        const auto picAfter = fresh();
        int lightRows = 0;
        if (picBefore.isValid() && picAfter.isValid() && picBefore.getBounds() == picAfter.getBounds())
        {
            const juce::Image::BitmapData a (picBefore, juce::Image::BitmapData::readOnly), c (picAfter, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < a.height; ++y) if (std::memcmp (a.getLinePointer (y), c.getLinePointer (y), (size_t) a.width * 4) != 0) ++lightRows;
        }
        const int b2 = st->builds();
        sc.repaint(); fresh();                                          // and all of the sheet printed with them there: is it what it was?
        int sheetRows = 0;
        {
            const juce::Image::BitmapData a (before, juce::Image::BitmapData::readOnly), c (st->sheet(), juce::Image::BitmapData::readOnly);
            for (int y = 0; y < a.height; ++y) if (std::memcmp (a.getLinePointer (y), c.getLinePointer (y), (size_t) a.width * 4) != 0) ++sheetRows;
        }
        ses.userSet (cx::lane (1, cx::kMute), 1.0f); audio (4); fresh();
        const int b3 = st->builds();
        ses.userSet (cx::lane (1, cx::kMute), 0.0f); audio (4); fresh();
        check (b1 == b0 && b2 == b0 && sheetRows == 0 && b3 == b0 + 1 && lightRows > 50,
               "the shapes are built and sent to the card only when a part changes: not for a refresh with nothing new, the meters, or 72 knobs travelling (" + juce::String (b1 - b0) + ", " + juce::String (b2 - b0)
                   + " builds; the sheet under the knobs is as it was, " + juce::String (sheetRows) + " rows differ; their lights did move in the picture, over " + juce::String (lightRows) + " rows), and once for a key (" + juce::String (b3 - b0) + ")");
    }

    // 4. the eye
    {
        const auto home0 = st->home();
        const juce::Point<float> off (20.0f, 20.0f);       // off the instrument: nothing there
        sc.mouseDown (ev (off, off, false)); sc.mouseDrag (ev (off + juce::Point<float> (80.0f, 30.0f), off, true)); sc.mouseUp (ev (off + juce::Point<float> (80.0f, 30.0f), off, true));
        check (st->home().turn == home0.turn && st->home().tilt == home0.tilt && sc.eye().locked, "locked, a drag off the controls leaves the view alone");
        sc.keyPressed (juce::KeyPress ('k', juce::ModifierKeys(), 'k'));
        sc.mouseDown (ev (off, off, false)); sc.mouseDrag (ev (off + juce::Point<float> (80.0f, 30.0f), off, true)); sc.mouseUp (ev (off + juce::Point<float> (80.0f, 30.0f), off, true));
        const auto turned = st->home();
        check (! sc.eye().locked && turned.view == -1 && std::abs (turned.turn - home0.turn) > 5.0f && std::abs (turned.tilt - home0.tilt) > 2.0f, "K unlocks it: the same drag turns the instrument (" + juce::String (turned.turn, 1) + " round, " + juce::String (turned.tilt, 1) + " up)");
        juce::MouseWheelDetails wheel; wheel.deltaX = 0.0f; wheel.deltaY = 0.5f; wheel.isReversed = false; wheel.isSmooth = false; wheel.isInertial = false;
        sc.mouseWheelMove (ev (off, off, false), wheel);
        check (st->home().away < turned.away * 0.95f, "and the wheel brings it nearer");
        {
            const int ctl = cx::lane (0, 5);
            const float was = ses.value (ctl);
            fresh();
            const auto p = pt (find ([ctl] (const Screen::TestHit& h) { return Screen::testIsControl (h, ctl); }));
            sc.mouseWheelMove (ev (p, p, false), wheel);
            check (ses.value (ctl) > was && st->home().away == sc.eye().pose.away, "over a knob the wheel still turns the knob");
        }
        sc.keyPressed (juce::KeyPress ('k', juce::ModifierKeys(), 'k'));
        check (sc.eye().locked && sc.eye().pose.turn == st->home().turn, "K again locks it where it was put");
        sc.keyPressed (juce::KeyPress ('v', juce::ModifierKeys(), 'v'));
        check (st->home().view == 1, "V goes to a view of the whole instrument");
        sc.keyPressed (juce::KeyPress ('v', juce::ModifierKeys(), 'v'));
        check (st->home().view == 2, "and V again to the next");
        view (1);
    }

    // 5. following: a knob under a hand on the controller, a snapshot, and what must not move the eye
    {
        auto restFor = [&] (int ms) { const auto t0 = juce::Time::getMillisecondCounter(); while (juce::Time::getMillisecondCounter() - t0 < (juce::uint32) ms) { audio (4); sc.testTick(); pump (20); } };
        fresh(); restFor (100);
        const int ctl = cx::lane (7, 6);
        const auto laneRect = [&] (int lane) { for (auto& h : sc.testHits()) if (Screen::testIsPanelOf (h, { 0, lane, 0 })) return h.r; return juce::Rectangle<float>(); };
        fromOutside (ctl, 0.8f, 1); sc.testTick(); sc.testTick();
        const auto box = sc.testFollowing();
        check (! box.isEmpty() && box.contains (laneRect (7)) && box.getWidth() < laneRect (7).getWidth() + 40.0f, "a knob turned on the controller: the eye goes to its module (" + box.toString() + ")");
        check (knobTags().size() == 1 && knobTags()[0] == ctl && sc.testTagText (ctl).startsWith ("CUTOFF|"), "and its tag is up: " + sc.testTagText (ctl));
        fresh();
        check (st->isAway(), "the eye has left home");
        restFor (1500);
        check (! sc.testFollowing().isEmpty() && knobTags().empty(), "the tag goes; the eye stays a moment longer");
        restFor (1500);
        check (sc.testFollowing().isEmpty(), "left alone, it goes home");
        // a sequencer's own move: nothing
        const int seq = cx::lane (2, 3);
        fromOutside (seq, 0.9f, 2); sc.testTick(); sc.testTick();
        check (sc.testFollowing().isEmpty() && knobTags().empty(), "a knob moved from a sequencer's port moves no eye and raises no tag");
        // and several of them at once (a sequencer's automation of a whole lane) are still its own
        for (int k : { 0, 1, 2, 4 }) fromOutside (cx::lane (3, k), 0.6f, 2);
        sc.testTick(); sc.testTick();
        check (sc.testFollowing().isEmpty() && knobTags().empty(), "nor do several moved from a sequencer's port at once");
        // a key (a mute pulsed from the controller): nothing
        ses.moved (cx::lane (1, cx::kMute), 1); ses.userSet (cx::lane (1, cx::kMute), 1.0f); audio (4); sc.testTick(); sc.testTick();
        check (sc.testFollowing().isEmpty() && knobTags().empty(), "a key pressed on the controller moves no eye");
        ses.userSet (cx::lane (1, cx::kMute), 0.0f);
        // the tempo, under an outside clock: nothing
        ses.userSet (cx::kTempo, 0.5f); audio (4); sc.testTick(); sc.testTick();
        check (sc.testFollowing().isEmpty(), "the tempo moving moves no eye");
        restFor (1000);
        // several knobs moving together with no hand on them: a snapshot. The eye goes out to hold them all.
        for (int lane : { 0, 1 }) for (int k : { 1, 4, 6 }) e.set (cx::lane (lane, k), 0.15f + 0.1f * (float) k, 0.0);
        audio (4); sc.testTick();
        const auto wide = sc.testFollowing();
        check (! wide.isEmpty() && wide.contains (laneRect (0)) && wide.contains (laneRect (1)) && ! wide.contains (laneRect (7)), "a snapshot moving two lanes' knobs: the eye goes out to hold both (" + wide.toString() + ")");
        check (knobTags().empty(), "with no tag on any of them");
        // FOLLOW off
        sc.keyPressed (juce::KeyPress ('m', juce::ModifierKeys(), 'm'));
        restFor (2600);
        fromOutside (ctl, 0.3f, 1); sc.testTick(); sc.testTick();
        check (sc.testFollowing().isEmpty() && knobTags().size() == 1, "M: the eye stays home, and the tag still comes up");
        sc.keyPressed (juce::KeyPress ('m', juce::ModifierKeys(), 'm'));
        restFor (3200);
        check (sc.testFollowing().isEmpty(), "M again: it follows again, and with nothing moving it is home");
    }

    // 6. INFO: the box floats by the pointer
    {
        sc.setInfo (true); fresh();
        const int ctl = cx::lane (6, 3);
        const auto p = pt (find ([ctl] (const Screen::TestHit& h) { return Screen::testIsControl (h, ctl); }));
        sc.mouseMove (ev (p, p, false)); sc.testTick();
        const auto tags = sc.testTags();
        check (std::find (tags.begin(), tags.end(), -1) != tags.end() && sc.testTip().contains ("RUMBLE"), "INFO: a knob pointed at says what it is, in a box by the pointer");
        check (std::find (tags.begin(), tags.end(), -3) != tags.end(), "and the list of the keyboard's keys is up with it, at the foot of the window");
        sc.setInfo (false); fresh();
        const auto after = sc.testTags();
        check (std::find (after.begin(), after.end(), -3) == after.end(), "INFO off: the list is gone");
    }

    {
        // the glow under the case: its colour and brightness are SETUP's, and as it comes nothing has changed
        auto rowsApart = [] (const juce::Image& x, const juce::Image& y)
        {
            if (! x.isValid() || x.getBounds() != y.getBounds()) return -1;
            const juce::Image::BitmapData a (x, juce::Image::BitmapData::readOnly), c (y, juce::Image::BitmapData::readOnly);
            int n = 0;
            for (int yy = 0; yy < a.height; ++yy) if (std::memcmp (a.getLinePointer (yy), c.getLinePointer (yy), (size_t) a.width * 4) != 0) ++n;
            return n;
        };
        pump (3400); fresh();
        const auto asItComes = fresh();
        sc.setGlow (0.58f, 1.0f);
        const auto blue = fresh();
        sc.setGlow (Stage::kGlowHue, 0.0f);
        const auto none = fresh();
        sc.setGlow (Stage::kGlowHue, Stage::kGlowBright);
        const auto back = fresh();
        // the pulse: with the kick's meter up the glow is brighter than it is steady, and with it down, dimmer
        auto lightOf = [] (const juce::Image& x)
        {
            if (! x.isValid()) return 0.0;
            const juce::Image::BitmapData a (x, juce::Image::BitmapData::readOnly);
            double sum = 0.0;
            for (int yy = 0; yy < a.height; yy += 4) for (int xx = 0; xx < a.width; xx += 4) { const auto* q = a.getPixelPointer (xx, yy); sum += q[0] + q[1] + q[2]; }
            return sum;
        };
        sc.setGlow (Stage::kGlowHue, Stage::kGlowBright, 1.0f);
        e.laneDb[0] = -6.0f; fresh();
        const double onTheHit = lightOf (fresh());
        e.laneDb[0] = -70.0f; fresh();
        const double between = lightOf (fresh());
        sc.setGlow (Stage::kGlowHue, Stage::kGlowBright, 0.0f);
        const double steady = lightOf (fresh());
        check (onTheHit > steady * 1.01 && between < steady * 0.995, "PULSE: the glow is brighter on the kick and dimmer between hits than it is steady (the picture's light: "
                   + juce::String (onTheHit / steady, 3) + " and " + juce::String (between / steady, 3) + " of steady)");
        const auto back2 = fresh();
        check (rowsApart (asItComes, back2) == 0, "and with PULSE at none the picture is as it was");
        const int toBlue = rowsApart (asItComes, blue), toNone = rowsApart (asItComes, none), again = rowsApart (asItComes, back);
        check (toBlue > 100 && toNone > 100 && again == 0, "the glow under the case takes SETUP's colour and brightness (" + juce::String (toBlue) + " rows of the picture change for blue and bright, " + juce::String (toNone) + " for none), and put back it is as it was (" + juce::String (again) + ")");
    }

    // 7. the dark instrument, and back to the lit skin and the flat drawing
    {
        const auto light = fresh();
        if (const auto dump = juce::SystemStats::getEnvironmentVariable ("SF000_STAGETEST_PNG", ""); dump.isNotEmpty())
        { juce::File f (dump); f.deleteFile(); juce::FileOutputStream out (f); juce::PNGImageFormat png; png.writeImageToStream (light, out); }
        sc.setDark (true);
        const auto dark = fresh();
        const auto mid = pt ({ 700.0f, 38.0f }).roundToInt();       // (bare case, on the top bar)
        check (light.getPixelAt (mid.x, mid.y).getBrightness() > dark.getPixelAt (mid.x, mid.y).getBrightness() + 0.3f,
               "D: the dark instrument (the case at " + mid.toString() + ": " + juce::String (light.getPixelAt (mid.x, mid.y).getBrightness(), 2) + " light, " + juce::String (dark.getPixelAt (mid.x, mid.y).getBrightness(), 2) + " dark)");
        sc.setDark (false);
        sc.keyPressed (juce::KeyPress ('3', juce::ModifierKeys(), '3'));
        pump (100);
        const auto flat = sc.createComponentSnapshot (sc.getLocalBounds(), false, 0.5f);
        check (! sc.isThree() && ! sc.threeShowing() && flat.getPixelAt (200, 150).getBrightness() > 0.3f, "3: back to the lit skin, drawn as before");
        sc.keyPressed (juce::KeyPress ('3', juce::ModifierKeys(), '3'));
        for (int i = 0; i < 400 && sc.isThree() && ! sc.threeShowing(); ++i) pump (25);
        check (sc.threeShowing(), "3 again: the stage is back");
        st = sc.stageOrNull();
        fresh();
    }

    // 8. how long things take here (a software driver: the frame is no guide to a graphics card; the rest is)
    {
        double rec = 1.0e9, printAll = 1.0e9, printPart = 1.0e9, drawn = 0.0, sent = 0.0;
        for (int i = 0; i < 5; ++i)
        {
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            sc.refreshNow();
            rec = std::min (rec, juce::Time::getMillisecondCounterHiRes() - t0);
            sc.repaint();
            t0 = juce::Time::getMillisecondCounterHiRes();
            sc.refreshNow();
            if (juce::Time::getMillisecondCounterHiRes() - t0 < printAll) { printAll = juce::Time::getMillisecondCounterHiRes() - t0; drawn = sc.testPrintMs(); sent = sc.testSendMs(); }
            ses.userSet (cx::lane (3, 2), 0.1f * (float) (i + 1), true); audio (2); sc.testTick();
            t0 = juce::Time::getMillisecondCounterHiRes();
            sc.refreshNow();
            printPart = std::min (printPart, juce::Time::getMillisecondCounterHiRes() - t0);
        }
        (void) fresh();
        std::printf ("  the screen recorded: %.2f ms; recorded and all of the sheet printed: %.1f ms (drawn %.1f, handed over %.1f); recorded and a knob's part printed: %.2f ms; a frame: %.0f ms\n", rec, printAll, drawn, sent, printPart, st->frameMs());
    }
    sc.setThree (false);
    sc.removeFromDesktop();
    std::printf (bad == 0 ? "all passed\n" : "%d FAILED\n", bad);
    return bad == 0 ? 0 : 1;
}

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (juce::Component* content)
        : DocumentWindow ("SF-000", juce::Colour (0xff2B2B28), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, false);
        setResizable (true, false);
        // as close to 1728 x 1117 as the display allows, keeping the shape
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        const float s = std::min (1.0f, std::min ((float) area.getWidth() / sf0::Screen::kW, (float) (area.getHeight() - 30) / sf0::Screen::kH));
        centreWithSize ((int) (sf0::Screen::kW * s), (int) (sf0::Screen::kH * s));
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

// AUDIO, set once before the set: the interface, its outputs and buffer; and under it MIDI: what is
// connected, the clock out, and how far ahead of the sound the clock has to run for a follower's
// notes to land on the beat.
class SetupPanel : public juce::Component, private juce::Timer
{
public:
    SetupPanel (juce::AudioDeviceManager& dm, sf0::Engine& e, sf0::Midi& m)
        : audio (dm, 0, 0, 2, 4, false, false, true, false), engine (e), midi (m)
    {
        addAndMakeVisible (audio);
        for (auto* l : { &load, &ins, &e16, &delay, &clockLabel, &status, &stepsLabel, &late, &knobLabel })
        {
            addAndMakeVisible (*l);
            l->setColour (juce::Label::textColourId, juce::Colour (0xffE8E5E1));
            l->setFont (juce::FontOptions (14.0f));
            l->setMinimumHorizontalScale (1.0f);
        }
        late.setJustificationType (juce::Justification::topLeft);
        delay.setJustificationType (juce::Justification::topLeft);
        clockLabel.setText ("CLOCK", juce::dontSendNotification);
        stepsLabel.setText ("OWN STEPS", juce::dontSendNotification);
        auto radio = [this] (juce::TextButton& b, const char* text, int group, std::function<void()> chosen)
        {
            addAndMakeVisible (b);
            b.setButtonText (text);
            b.setRadioGroupId (group);
            b.setClickingTogglesState (true);
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4D514E));
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff428F47));
            b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffE8E5E1));
            b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffE8E5E1));
            b.onClick = [&b, chosen] { if (b.getToggleState()) chosen(); };
        };
        // the clock is sent, followed, or neither
        radio (off, "OFF", 1,       [this] { midi.setFollowsClock (false); midi.setSendsClock (false); show(); });
        radio (send, "SEND", 1,     [this] { midi.setFollowsClock (false); midi.setSendsClock (true); show(); });
        radio (follow, "FOLLOW", 1, [this] { midi.setFollowsClock (true); show(); });
        (midi.followsClock() ? follow : midi.sendsClock() ? send : off).setToggleState (true, juce::dontSendNotification);
        // following: where the rig's own steps are heard
        radio (withNotes, "WITH NOTES IN", 2, [this] { midi.setOnBeat (false); });
        radio (onBeat, "ON THE BEAT", 2,     [this] { midi.setOnBeat (true); });
        (midi.onBeat() ? onBeat : withNotes).setToggleState (true, juce::dontSendNotification);
        for (auto* sl : { &lead, &trim })
        {
            addAndMakeVisible (*sl);
            sl->setTextValueSuffix (" ms");
            sl->setSliderStyle (juce::Slider::LinearHorizontal);
            sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        }
        lead.setRange (0.0, 40.0, 0.5);
        lead.setValue (midi.lead(), juce::dontSendNotification);
        lead.onValueChange = [this] { midi.setLead ((float) lead.getValue()); };
        trim.setRange (-30.0, 30.0, 0.5);
        trim.setValue (midi.trim(), juce::dontSendNotification);
        trim.setDoubleClickReturnValue (true, 0.0);
        trim.onValueChange = [this] { midi.setTrim ((float) trim.getValue()); };
        // ---- the look: full screen, and the glow under the case (the 3D stage)
        for (auto* l : { &lookLabel, &hueLabel, &brightLabel })
        {
            addAndMakeVisible (*l);
            l->setColour (juce::Label::textColourId, juce::Colour (0xffE8E5E1));
            l->setFont (juce::FontOptions (14.0f));
        }
        lookLabel.setText ("LOOK", juce::dontSendNotification);
        for (auto* l : { &glowLabel, &pulseLabel })
        {
            addAndMakeVisible (*l);
            l->setColour (juce::Label::textColourId, juce::Colour (0xffE8E5E1));
            l->setFont (juce::FontOptions (14.0f));
        }
        glowLabel.setText ("UNDERGLOW", juce::dontSendNotification);
        hueLabel.setText ("COLOUR", juce::dontSendNotification);
        brightLabel.setText ("BRIGHTNESS", juce::dontSendNotification);
        pulseLabel.setText ("PULSE (KICK)", juce::dontSendNotification);
        glowPulse.setDoubleClickReturnValue (true, 0.0);
        for (auto* k : { &glowHue, &glowBright, &glowPulse })
        {
            addAndMakeVisible (*k);
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            k->setRange (0.0, 1.0);
            k->onValueChange = [this] { if (onGlow) onGlow ((float) glowHue.getValue(), (float) glowBright.getValue(), (float) glowPulse.getValue()); };
        }
        glowHue.setDoubleClickReturnValue (true, sf0::Stage::kGlowHue);
        glowBright.setDoubleClickReturnValue (true, sf0::Stage::kGlowBright);
        addAndMakeVisible (fullScreen);
        fullScreen.setButtonText ("FULL SCREEN");
        fullScreen.onClick = [this] { if (onFull) onFull(); };
        setSize (520, 850);
        show();
        startTimerHz (4);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        audio.setBounds (r.removeFromTop (380));
        r = r.reduced (16, 4);
        load.setBounds (r.removeFromTop (24));
        ins.setBounds (r.removeFromTop (24));
        e16.setBounds (r.removeFromTop (24));
        r.removeFromTop (4);
        delay.setBounds (r.removeFromTop (70));
        r.removeFromTop (4);
        auto row = r.removeFromTop (26);
        clockLabel.setBounds (row.removeFromLeft (110));
        for (auto* b : { &off, &send, &follow }) { b->setBounds (row.removeFromLeft (96)); row.removeFromLeft (6); }
        r.removeFromTop (4);
        status.setBounds (r.removeFromTop (24));
        row = r.removeFromTop (26);
        stepsLabel.setBounds (row.removeFromLeft (110));
        for (auto* b : { &withNotes, &onBeat }) { b->setBounds (row.removeFromLeft (147)); row.removeFromLeft (6); }
        r.removeFromTop (4);
        row = r.removeFromTop (28);
        knobLabel.setBounds (row.removeFromLeft (110));
        lead.setBounds (row); trim.setBounds (row);
        r.removeFromTop (2);
        auto look = r.removeFromBottom (96);
        late.setBounds (r);
        look.removeFromTop (8);
        auto top = look.removeFromTop (26);
        lookLabel.setBounds (top.removeFromLeft (110));
        fullScreen.setBounds (top.removeFromLeft (147));
        look.removeFromTop (6);
        auto knobs = look;
        glowLabel.setBounds (knobs.removeFromLeft (110).removeFromTop (26));
        glowHue.setBounds (knobs.removeFromLeft (52));
        hueLabel.setBounds (knobs.removeFromLeft (66));
        glowBright.setBounds (knobs.removeFromLeft (52));
        brightLabel.setBounds (knobs.removeFromLeft (92));
        glowPulse.setBounds (knobs.removeFromLeft (52));
        pulseLabel.setBounds (knobs);
    }
    // the look row: where its knobs stand, and what turning them or pressing FULL SCREEN does
    std::function<void (float hue, float bright, float pulse)> onGlow;
    std::function<void()> onFull;
    void setGlow (float hue, float bright, float pulse)
    {
        glowHue.setValue (hue, juce::dontSendNotification); glowBright.setValue (bright, juce::dontSendNotification); glowPulse.setValue (pulse, juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff4D514E));
        g.fillRect (16, 382, getWidth() - 32, 1);
    }

private:
    // what the chosen clock needs on show
    void show()
    {
        const bool fol = midi.followsClock(), snd = ! fol && midi.sendsClock();
        for (auto* c : std::initializer_list<juce::Component*> { &stepsLabel, &onBeat, &withNotes, &trim }) c->setVisible (fol);
        lead.setVisible (snd);
        knobLabel.setVisible (fol || snd);
        knobLabel.setText (fol ? "TRIM" : "CLOCK LEAD", juce::dontSendNotification);
        timerCallback();
    }
    void timerCallback() override
    {
        auto list = [] (const juce::StringArray& n) { return n.isEmpty() ? juce::String ("NONE") : n.joinIntoString (", "); };
        auto ms = [] (float v) { return juce::String (v, 1); };
        // The rig's own delay, whatever the clock does: the figure to put on outside gear (or on
        // the sequencers that play it) to hear it with SF-000. Worked out, not measured: the
        // interface's part is what the system reports for it.
        const juce::String total = ms (engine.soundsAfterMs());
        delay.setText ("LATENCY: " + total + " MS FROM A NOTE ARRIVING TO ITS SOUND (" + ms (engine.waitPartMs()) + " WAITING FOR A BUFFER, " + ms (engine.outPartMs())
                           + " OUT THROUGH THE INTERFACE, " + ms (engine.insidePartMs()) + " THE LIMITER'S LOOK-AHEAD). DELAY OUTSIDE GEAR BY THAT TO HEAR IT WITH SF-000. ONLY THE BUFFER SIZE CHANGES IT.",
                       juce::dontSendNotification);
        load.setText ("ENGINE: " + juce::String (juce::roundToInt (100.0 * engine.cpu.load())) + " % OF EACH BUFFER'S TIME (THE SOUND'S SHARE OF ONE CORE)", juce::dontSendNotification);
        ins.setText ("MIDI IN: " + list (midi.inputNames()) + "   (A LANE'S NUMBER IS ITS CHANNEL)", juce::dontSendNotification);
        e16.setText ("E16: " + (midi.e16Name().isNotEmpty() ? midi.e16Name() : juce::String ("NOT FOUND")), juce::dontSendNotification);
        const bool fol = midi.followsClock(), snd = ! fol && midi.sendsClock();
        const int n = engine.notesHeard();
        const juce::String landed = "NOTES IN LAND " + juce::String (std::abs (engine.notesLateMs()), 1) + " MS " + (engine.notesLateMs() >= 0.0f ? "LATE" : "EARLY");
        if (fol)
        {
            const auto src = midi.clockSource();
            status.setText (engine.following() ? "FOLLOWING " + (src.isNotEmpty() ? src.toUpperCase() : juce::String ("A CLOCK")) + ": " + juce::String (engine.followedBpm(), 1) + " BPM. ITS START AND STOP ARE THE TRANSPORT."
                                               : juce::String ("NO CLOCK IS COMING IN: SF-000 RUNS ON ITS OWN TEMPO."), juce::dontSendNotification);
            late.setText ((midi.onBeat() ? "ON THE BEAT: SF-000'S OWN STEPS ARE PLAYED THAT MUCH EARLY, SO THEY ARE HEARD WITH GEAR THE SENDER PLAYS DIRECTLY. LANES IT PLAYS BY MIDI ARE HEARD " + total + " MS LATER."
                                               : "WITH NOTES IN: SF-000'S OWN STEPS ARE HEARD WITH THE LANES THE SENDER PLAYS BY MIDI. GEAR IT PLAYS DIRECTLY IS HEARD " + total + " MS BEFORE BOTH.")
                              + (n > 0 ? "\n" + landed + " AGAINST SF-000'S OWN STEPS (" + juce::String (n) + " NOTES)." : juce::String()),
                          juce::dontSendNotification);
        }
        else if (snd)
        {
            status.setText ("CLOCK TO: " + list (midi.clockNames()) + "   (START, 24 TICKS A BEAT, STOP, WHILE IT RUNS)", juce::dontSendNotification);
            late.setText (n == 0 ? juce::String ("NOTES IN: NONE YET WHILE THE CLOCK RUNS. PLAY A PLAIN SIXTEENTH PATTERN INTO A LANE TO SEE HOW LATE IT LANDS.")
                                 : landed + " (AGAINST THE SIXTEENTHS, " + juce::String (n) + " NOTES). ADD THAT TO CLOCK LEAD, OR TAKE IT OFF.",
                          juce::dontSendNotification);
        }
        else
        {
            status.setText ("NO CLOCK IS SENT OR FOLLOWED.", juce::dontSendNotification);
            late.setText ({}, juce::dontSendNotification);
        }
    }
    juce::AudioDeviceSelectorComponent audio;
    sf0::Engine& engine;
    sf0::Midi& midi;
    juce::Label load, ins, e16, delay, clockLabel, status, stepsLabel, late, knobLabel;
    juce::TextButton off, send, follow, onBeat, withNotes;
    juce::Slider lead, trim;
    juce::Label lookLabel, glowLabel, hueLabel, brightLabel, pulseLabel;
    juce::Slider glowHue, glowBright, glowPulse;
    juce::TextButton fullScreen;
};

// What the profile makes of the rig in a few states: each snapshot it comes with, and some plain
// cases to judge the numbers by. SF-000 --profiles
static int profiles()
{
    using namespace sf0;
    Engine e;
    e.prepare (48000.0, 256);
    Session ses (e, false);
    juce::AudioBuffer<float> b (2, 256);
    auto run = [&] (double bars) { const int blocks = (int) (bars * 4.0 * 60.0 / e.tempo() * 48000.0 / 256.0); for (int k = 0; k < blocks; ++k) e.render (b); };
    auto show = [&] (const juce::String& name)
    {
        Engine::Heard h;
        if (! e.lastHeard (h)) { std::printf ("  %-22s nothing heard\n", name.toRawUTF8()); return; }
        const auto p = profileOf (h);
        juce::String bands, low, top, lanesOn;
        for (auto v : p.bands) bands << juce::String::charToString (" .:-=+*#@"[juce::jlimit (0, 8, (int) (v * 8.99f))]);
        for (int s = 0; s < 16; ++s) { low << juce::String::charToString (" .oO@"[juce::jlimit (0, 4, (int) (p.pulse[(size_t) s] * 4.99f))]); top << juce::String::charToString (" .oO@"[juce::jlimit (0, 4, (int) (p.pulse[(size_t) (16 + s)] * 4.99f))]); }
        for (auto v : p.lanes) lanesOn << juce::String::charToString (" .oO@"[juce::jlimit (0, 4, (int) (v * 4.99f))]);
        std::printf ("  %-22s energy %.2f weight %.2f bright %.2f density %.2f tone %.2f move %.2f wet %.2f | %d bars %.1f bpm mix %.1f dB | [%s] low[%s] top[%s] lanes[%s]\n",
                     name.toRawUTF8(), p.energy, p.weight, p.bright, p.density, p.tone, p.movement, p.wet, p.bars, p.bpm, h.mixDb,
                     bands.toRawUTF8(), low.toRawUTF8(), top.toRawUTF8(), lanesOn.toRawUTF8());
    };
    std::printf ("SF-000 profiles\n");
    e.setRunning (true);
    for (size_t i = 0; i < ses.library.size(); ++i)
    {
        ses.recall (ses.library[i].id);
        run (7.0);
        show (ses.library[i].name);
    }
    // plain cases, from the rig as it first comes up
    auto only = [&] (std::initializer_list<int> keep, const char* name)
    {
        for (auto& s : ses.library) if (s.name == "INIT") ses.recall (s.id);
        for (int l = 0; l < kLanes; ++l) ses.userSet (cx::lane (l, cx::kMute), std::find (keep.begin(), keep.end(), l) != keep.end() ? 0.0f : 1.0f);
        run (7.0);
        show (name);
        for (int l = 0; l < kLanes; ++l) ses.userSet (cx::lane (l, cx::kMute), 0.0f);
    };
    only ({ 0 }, "kick alone");
    only ({ 0, 5 }, "kick and bass");
    only ({ 0, 1, 2, 3, 4 }, "drums");
    only ({ 9, 10 }, "pad and voice");
    only ({ 7 }, "lead alone");
    only ({ 1, 2 }, "hats alone");
    e.setRunning (false);
    return 0;
}

// The AUDIO AND MIDI panel drawn to a file: --shot f.png --setup [send|follow|off] [--beat] (a clock is fed in for follow).
static int shotSetup (const juce::StringArray& args)
{
    using namespace sf0;
    auto arg = [&] (const char* name, juce::String def) { const int i = args.indexOf (name); return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : def; };
    const auto mode = arg ("--setup", "follow");
    juce::AudioDeviceManager dm;
    Engine e;
    e.prepare (48000.0, 128);
    Session ses (e, false);
    Midi m (e, ses, false);
    m.setFollowsClock (mode == "follow");
    m.setSendsClock (mode == "send");
    m.setOnBeat (args.contains ("--beat"));
    e.setOutputDelayForTests (4.1f);
    juce::AudioBuffer<float> b (2, 128);
    int port = 0;
    const double per = 60000.0 / (24.0 * 126.0), dur = 128.0 * 1000.0 / 48000.0;
    double next = 20.0;
    for (int k = 0; (double) k * dur < 2000.0; ++k)
    {
        if (mode == "follow") for (; next <= (double) k * dur; next += per) { if (next == 20.0) m.clockFrom (&port, "OXI ONE", 0xFA, next - 1.0); m.clockFrom (&port, "OXI ONE", 0xF8, next); }
        e.render (b);
    }
    int other = 0;
    if (mode == "follow") m.clockFrom (&other, "OXI ONE", 0xFE, juce::Time::getMillisecondCounterHiRes());     // (so the panel finds a source still sending)
    SetupPanel panel (dm, e, m);
    const float scale = (float) juce::jlimit (0.25, 4.0, arg ("--scale", "2").getDoubleValue());
    juce::Image img (juce::Image::ARGB, juce::roundToInt ((float) panel.getWidth() * scale), juce::roundToInt ((float) panel.getHeight() * scale), true);
    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff2B2B28));
        g.addTransform (juce::AffineTransform::scale (scale));
        panel.paintEntireComponent (g, true);
    }
    juce::File f (arg ("--shot", "sf000-setup.png"));
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream (img, out)) { std::printf ("could not write %s\n", f.getFullPathName().toRawUTF8()); return 1; }
    std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}

// ONLINE against a snapshot library that is running: the site's own `npm run dev`.
//   SF-000 --onlinetest http://127.0.0.1:8787 [http://127.0.0.1:8788]
// The second address, if given, is a library that lets nobody in (the turned-away path).
static int onlineTest (const juce::String& base, const juce::String& refusing)
{
    using namespace sf0;
    using juce::var;
    int bad = 0;
    auto check = [&bad] (bool ok, const juce::String& what) { std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++bad; };
    struct Got { int status = 0; juce::MemoryBlock bytes; var json; };
    auto ask = [&base] (const juce::String& method, const juce::String& path)
    {
        Got g;
        const auto in = juce::URL (base + path).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                                     .withStatusCode (&g.status).withHttpRequestCmd (method).withConnectionTimeoutMs (10000));
        if (in != nullptr) { in->readIntoMemoryBlock (g.bytes); g.json = juce::JSON::parse (g.bytes.toString()); }
        return g;
    };
    std::printf ("SF-000 online test (%s)\n", base.toRawUTF8());
    if (ask ("GET", "/api/health").status != 200) { std::printf ("  no snapshot library answers there: start the site's `npm run dev` first\n"); return 1; }

    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_online_test");
    tmp.deleteRecursively();
    Session::stateFileForTests = tmp.getChildFile ("session.json");
    juce::StringArray ours;
    {
        Engine e;
        e.prepare (48000.0, 128);
        Session ses (e, true);
        juce::AudioBuffer<float> b (2, 128);
        auto bars = [&] (double n) { const int blocks = (int) (n * 4.0 * 60.0 / e.tempo() * 48000.0 / 128.0); for (int k = 0; k < blocks; ++k) e.render (b); };
        auto take = [&] { const auto id = ses.take(); const auto uid = ses.findSnap (id)->uid; ours.add (uid); return std::make_pair (id, uid); };
        auto there = [&] (const juce::String& uid) { return ask ("GET", "/api/snapshots/" + uid); };
        for (int k = 0; k < 400; ++k) e.render (b);
        e.setRunning (true);
        bars (5.2);

        Online::Setup st;
        st.url = base; st.clientId = "sf-000-test.access"; st.clientSecret = "not-a-real-secret";
        {
            Online on (ses);
            // 1. ONLINE out: a snapshot stays at home, and is counted
            const auto a = take();
            ses.waitForClips();
            on.pass();
            check (on.status().state == Online::State::off && on.status().waiting == 1 && on.requests() == 0 && there (a.second).status == 404,
                   "with ONLINE out a snapshot taken stays at home, counted as waiting, and nothing is asked of the library");
            on.setOn (true);
            on.pass();
            check (on.status().state == Online::State::notSetUp && on.requests() == 0, "lit with nothing set up, it says so and asks nothing");

            // 2. set up: the one that waited goes, whole, with its clip and what it was made with
            check (Online::check (st).isEmpty(), "CHECK: the library answers for these settings");
            st.on = true;
            on.setSetup (st);
            on.pass();
            auto g = there (a.second);
            const var snap = g.json["snapshot"];
            const auto lanePrint = snap["made"]["lanes"]["kick"].toString();
            const var defs = ask ("GET", "/api/defs?prints=" + lanePrint + "," + snap["made"]["chassis"].toString() + "," + snap["made"]["fx"]["DELAY"].toString()).json["defs"];
            juce::MemoryBlock local;
            ses.clipFile (a.first).loadFileAsData (local);
            const auto clip = ask ("GET", "/api/clips/" + a.second);
            const bool whole = g.status == 200 && snap["uid"].toString() == a.second && (juce::int64) snap["id"] == a.first && snap["values"].getDynamicObject() != nullptr
                               && snap["values"].getDynamicObject()->getProperties().size() > 200 && (bool) g.json["has_clip"] && (bool) g.json["has_profile"];
            const bool defined = defs[juce::Identifier (lanePrint)]["kind"].toString() == "lane" && defs[juce::Identifier (defs[juce::Identifier (lanePrint)]["engine"].toString())]["kind"].toString() == "engine"
                                 && defs[juce::Identifier (snap["made"]["chassis"].toString())]["kind"].toString() == "chassis" && defs[juce::Identifier (snap["made"]["fx"]["DELAY"].toString())]["kind"].toString() == "fx";
            check (whole && defined && local.getSize() > 1000 && clip.status == 200 && clip.bytes == local && on.status().state == Online::State::upToDate && on.status().sent == 1 && on.status().waiting == 0,
                   "set up and lit, the snapshot that waited goes: whole, its clip to the byte (" + juce::String ((int) (local.getSize() / 1024)) + " KB), and every definition it names, a lane's sound engine too");
            check (there (ses.libraryId + "-3e8").status == 404 && there (ses.libraryId + "-3e7").status == 404, "the snapshots the rig came with stay at home");

            // 3. taken while lit: the snapshot at once, its clip as soon as it is written
            bars (2.0);
            const auto bSnap = take();
            on.pass();
            const bool atOnce = there (bSnap.second).status == 200;
            ses.waitForClips();
            on.pass();
            check (atOnce && (bool) there (bSnap.second).json["has_clip"] && on.status().state == Online::State::upToDate && on.status().sent == 2,
                   "one taken while lit goes at once, and its clip follows when it has been written");
            const int before = on.requests();
            on.pass(); on.pass();
            check (on.requests() == before, "with nothing new, a pass asks the library nothing");

            // 4. no answer: it waits, and later sends only what is missing
            auto dead = st; dead.url = "http://127.0.0.1:9";
            on.setSetup (dead);
            bars (2.0);
            const auto c = take();
            ses.waitForClips();
            on.pass();
            const bool waits = on.status().state == Online::State::waiting && on.status().waiting >= 1 && on.line().contains ("TRIES AGAIN") && there (c.second).status == 404;
            on.setSetup (st);
            const int was = on.requests();
            on.pass();
            check (waits && (bool) there (c.second).json["has_clip"] && on.requests() - was == 3 && on.status().state == Online::State::upToDate && on.status().sent == 3,
                   "with no answer it waits and says so; when the library is back it asks what is held and sends only the one that is missing (3 requests: the question, the snapshot, its clip)");

            // 5. renamed here: its name there follows
            ses.rename (a.first, "roller");
            on.pass();
            check (there (a.second).json["name"].toString() == "ROLLER" && (bool) there (a.second).json["has_clip"], "a snapshot renamed here is renamed there, and keeps its clip");

            // 6. removed there: it is not sent again
            ask ("DELETE", "/api/snapshots/" + bSnap.second);
        }
        {
            Online again (ses);                                 // (as the app opening another day)
            const bool remembered = again.on() && again.setup().url == base && again.setup().clientSecret == st.clientSecret;
            again.pass();
            const int one = again.requests();
            again.pass();
            check (remembered && one == 1 && again.requests() == 1 && there (ours[1]).status == 404 && again.status().state == Online::State::upToDate && again.status().waiting == 0,
                   "opened again it remembers its settings and what was sent (one question, nothing resent), and one removed from the library is not sent back");

            // 7. turned away
            if (refusing.isNotEmpty())
            {
                auto shut = st; shut.url = refusing;
                const auto said = Online::check (shut);
                again.setSetup (shut);
                again.pass();
                check (said.contains ("DID NOT LET") && again.status().state == Online::State::refused && again.line().contains ("DID NOT LET"),
                       "a library that does not know the strings turns it away, and it says so (" + juce::String (again.status().note) + ")");
            }
        }
        const auto perms = Online::setupFile().existsAsFile();
        check (perms && ! juce::File (Online::setupFile()).loadFileAsString().contains ("\"secret\": \"\""), "the settings are kept in online.json beside the session");
    }
    for (auto& uid : ours) ask ("DELETE", "/api/snapshots/" + uid + "?forget=1");
    Session::stateFileForTests = juce::File();
    tmp.deleteRecursively();
    std::printf (bad == 0 ? "ALL ONLINE CHECKS PASSED\n" : "%d ONLINE CHECK(S) FAILED\n", bad);
    return bad == 0 ? 0 : 1;
}

// ONLINE's settings: where the snapshot library is, and the two strings that let SF-000 in. Typed
// or pasted here once; kept in online.json beside the session, for this user only.
class OnlinePanel : public juce::Component
{
public:
    explicit OnlinePanel (sf0::Online& o) : online (o)
    {
        const auto s = online.setup();
        auto row = [this] (juce::Label& l, const char* name, juce::TextEditor& e, const juce::String& value, const char* hint)
        {
            addAndMakeVisible (l); addAndMakeVisible (e);
            l.setText (name, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, juce::Colour (0xffE8E5E1));
            l.setFont (juce::FontOptions (14.0f));
            e.setFont (juce::FontOptions (14.0f));
            e.setText (value, false);
            e.setTextToShowWhenEmpty (hint, juce::Colour (0xff8D9794));
            e.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xffE8E5E1));
            e.setColour (juce::TextEditor::textColourId, juce::Colour (0xff2B2B28));
            e.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2B2B28));
            e.onTextChange = [this] { note.setText ({}, juce::dontSendNotification); };
        };
        row (lUrl, "ADDRESS", url, s.url, "https://...");
        row (lId, "CLIENT ID", id, s.clientId, "the service token's Client ID");
        secret.setPasswordCharacter (0x2022);
        row (lSecret, "CLIENT SECRET", secret, s.clientSecret, "the service token's Client Secret");
        for (auto* b : { &check, &keep })
        {
            addAndMakeVisible (*b);
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4D514E));
            b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffE8E5E1));
        }
        check.setButtonText ("CHECK");
        keep.setButtonText ("KEEP");
        addAndMakeVisible (note);
        note.setColour (juce::Label::textColourId, juce::Colour (0xffE8E5E1));
        note.setFont (juce::FontOptions (14.0f));
        note.setJustificationType (juce::Justification::topLeft);
        note.setText ("The address of your snapshot library, and the two strings Cloudflare showed when the service token was made. "
                      "CHECK asks the library whether they let SF-000 in. KEEP stores them on this Mac, readable by you only.", juce::dontSendNotification);
        // CHECK asks from a thread of its own, so the window stays alive while the network answers
        check.onClick = [this]
        {
            const auto s = entered();
            note.setText ("ASKING...", juce::dontSendNotification);
            juce::Component::SafePointer<OnlinePanel> me (this);
            juce::Thread::launch ([me, s]
            {
                const auto said = sf0::Online::check (s);
                juce::MessageManager::callAsync ([me, said] { if (me != nullptr) me->note.setText (said.isEmpty() ? juce::String ("THE LIBRARY LET SF-000 IN.") : said, juce::dontSendNotification); });
            });
        };
        keep.onClick = [this]
        {
            online.setSetup (entered());
            note.setText ("KEPT. " + juce::String (online.on() ? "ONLINE IS LIT: SNAPSHOTS GO AS THEY ARE TAKEN." : "LIGHT THE ONLINE KEY IN THE LIBRARY TO START SENDING."), juce::dontSendNotification);
        };
        setSize (560, 250);
    }

    void resized() override
    {
        int y = 14;
        for (auto pair : std::initializer_list<std::pair<juce::Label*, juce::TextEditor*>> { { &lUrl, &url }, { &lId, &id }, { &lSecret, &secret } })
        {
            pair.first->setBounds (14, y, 120, 26);
            pair.second->setBounds (138, y, getWidth() - 152, 26);
            y += 34;
        }
        check.setBounds (138, y + 2, 90, 28);
        keep.setBounds (236, y + 2, 90, 28);
        note.setBounds (14, y + 42, getWidth() - 28, getHeight() - y - 50);
    }

private:
    sf0::Online::Setup entered() const
    {
        auto s = online.setup();                // (ONLINE stays lit or out as it was)
        s.url = url.getText().trim(); s.clientId = id.getText().trim(); s.clientSecret = secret.getText().trim();
        return s;
    }
    sf0::Online& online;
    juce::Label lUrl, lId, lSecret, note;
    juce::TextEditor url, id, secret;
    juce::TextButton check, keep;
};

#if JUCE_LINUX
// Only for the Linux build, where the checks run. An X server with no window manager (Xvfb) has never
// heard the names of the window manager's properties. JUCE then asks about a property with no name,
// the server answers with an error, and if that answer is read by the OpenGL thread while this thread
// holds Xlib's lock, each waits for the other for good: one stage test in six stopped dead where the
// typing window opens. Saying the names first makes the server answer "not set", as on a real desktop.
// The connection stays open, or a server with no one else on it would forget them again.
static void nameTheWindowManagersProperties()
{
    static juce::DynamicLibrary x11;
    if (! x11.open ("libX11.so.6")) return;
    using Open = void* (*) (const char*);
    using Intern = unsigned long (*) (void*, const char*, int);
    const auto open = (Open) x11.getFunction ("XOpenDisplay");
    const auto intern = (Intern) x11.getFunction ("XInternAtom");
    if (open == nullptr || intern == nullptr) return;
    if (void* display = open (nullptr))
        for (const char* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "WM_CHANGE_STATE", "WM_STATE",
                                  "_NET_WM_PING", "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_COMBO",
                                  "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_ALLOWED_ACTIONS",
                                  "_NET_FRAME_EXTENTS", "_NET_WORKAREA", "_MOTIF_WM_HINTS" })
            intern (display, name, 0);
}
#endif

class SF000App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "SF-000"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& cmd) override
    {
        const auto args = juce::StringArray::fromTokens (cmd, true);
       #if JUCE_LINUX
        nameTheWindowManagersProperties();
       #endif
        if (args.contains ("--dump")) { dumpModules(); quit(); return; }
        if (args.contains ("--test"))
        {
            const int w = args.indexOf ("--wav");
            const int nb = args.indexOf ("--bars");
            setApplicationReturnValue (selfTest (w >= 0 && w + 1 < args.size() ? args[w + 1].unquoted() : juce::String(), nb >= 0 && nb + 1 < args.size() ? args[nb + 1].getDoubleValue() : 16.0));
            quit(); return;
        }
        if (args.contains ("--shot") && args.contains ("--setup")) { setApplicationReturnValue (shotSetup (args)); quit(); return; }
        if (args.contains ("--shot")) { setApplicationReturnValue (shot (args)); quit(); return; }
        if (args.contains ("--audit")) { const int i = args.indexOf ("--audit"); setApplicationReturnValue (knobAudit (i + 1 < args.size() ? args[i + 1].getIntValue() - 1 : -1)); quit(); return; }
        if (args.contains ("--uitest")) { setApplicationReturnValue (uiTest()); quit(); return; }
        if (args.contains ("--stagetest")) { setApplicationReturnValue (stageTest()); quit(); return; }
        if (args.contains ("--onlinetest"))
        {
            const int i = args.indexOf ("--onlinetest");
            setApplicationReturnValue (onlineTest (i + 1 < args.size() ? args[i + 1].unquoted() : juce::String ("http://127.0.0.1:8787"), i + 2 < args.size() ? args[i + 2].unquoted() : juce::String()));
            quit(); return;
        }
        if (args.contains ("--miditest")) { setApplicationReturnValue (midiTest()); quit(); return; }
        if (args.contains ("--profiles")) { setApplicationReturnValue (profiles()); quit(); return; }
        if (args.contains ("--perf")) { setApplicationReturnValue (perf()); quit(); return; }
        if (args.contains ("--e16")) { const int i = args.indexOf ("--e16"); setApplicationReturnValue (writeScene (i + 1 < args.size() ? args[i + 1].unquoted() : juce::String ("SF-000.oxie16"))); quit(); return; }

        engine = std::make_unique<sf0::Engine>();
        session = std::make_unique<sf0::Session> (*engine);
        std::unique_ptr<juce::XmlElement> saved (juce::XmlDocument::parse (audioFile()));
        const auto err = devices.initialise (0, 4, saved.get(), true);       // the set on 1 + 2, the headphones' cue on 3 + 4
        if (err.isNotEmpty()) juce::Logger::writeToLog ("SF-000 audio: " + err);
        devices.addAudioCallback (engine.get());
        midi = std::make_unique<sf0::Midi> (*engine, *session);
        recorder = std::make_unique<sf0::Recorder> (*engine);
        recorder->stateAtStart = [this] { session->save(); return sf0::Session::stateFile(); };
        // ONLINE: snapshots go to the library on the web as they are taken (if it is lit and set up)
        online = std::make_unique<sf0::Online> (*session);
        session->onLibrary = [this] { if (online != nullptr) online->poke(); };
        online->startSending();

        auto* screen = new sf0::Screen (*session);
        screen->recorder = recorder.get();
        screen->onAudioSetup = [this] { audioSetup(); };
        screen->online = online.get();
        screen->onOnlineSetup = [this] { onlineSetup(); };
        screen->onFullScreen = [this, screen]
        {
            // The window's own full screen, which goes both ways. (JUCE's kiosk mode did not: on the
            // Mac, with the system's title bar, it asks for full screen on the way out as on the
            // way in, so F and Esc could enter and nothing but quitting could leave.)
            if (window == nullptr) return;
            window->setFullScreen (! window->isFullScreen());
            juce::Timer::callAfterDelay (400, [safe = juce::Component::SafePointer<sf0::Screen> (screen)] { if (safe != nullptr) safe->grabKeyboardFocus(); });
        };
        screen->onLeaveFullScreen = [this] { if (window != nullptr && window->isFullScreen()) window->setFullScreen (false); };
        // The look last chosen (L: the lit skin or the flat drawing; D: its dark instrument) is kept between runs.
        {
            const juce::var look = juce::JSON::parse (lookFile());
            // (and the 3D stage: whether it is up, where its eye is kept, whether the view is locked, whether it follows)
            bool three = true;
            sf0::Screen::Eye eye;
            if (look.isObject())
            {
                screen->setLit ((bool) look.getProperty ("lit", true));
                screen->setDark ((bool) look.getProperty ("dark", false));
                three = (bool) look.getProperty ("three", true);
                if (const juce::var v = look.getProperty ("eye", juce::var()); v.isObject())
                {
                    auto num = [&v] (const char* name, float def) { const juce::var x = v.getProperty (name, juce::var()); return x.isVoid() ? def : (float) (double) x; };
                    eye.pose.view = juce::jlimit (-1, 2, (int) num ("view", 1.0f));
                    eye.pose.turn = juce::jlimit (-180.0f, 180.0f, num ("turn", 0.0f)); eye.pose.tilt = juce::jlimit (6.0f, 89.9f, num ("tilt", 56.0f));
                    eye.pose.away = juce::jlimit (260.0f, 16000.0f, num ("away", 4000.0f));
                    eye.pose.tx = num ("x", 0.0f); eye.pose.ty = num ("y", 0.0f); eye.pose.tz = num ("z", 0.0f);
                    eye.locked = (bool) v.getProperty ("locked", true); eye.follow = (bool) v.getProperty ("follow", true);
                }
            }
            screen->setEye (eye);
            if (look.isObject())
            {
                screen->setLoadFolder (false, look.getProperty ("load_lane", juce::var()).toString());
                screen->setLoadFolder (true, look.getProperty ("load_fx", juce::var()).toString());
            }
            if (look.isObject())
                screen->setGlow ((float) (double) look.getProperty ("glow_hue", sf0::Stage::kGlowHue), juce::jlimit (0.0f, 1.0f, (float) (double) look.getProperty ("glow_bright", sf0::Stage::kGlowBright)),
                                 juce::jlimit (0.0f, 1.0f, (float) (double) look.getProperty ("glow_pulse", 0.0)));
            screen->onLook = [screen]
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("lit", screen->isLit());
                o->setProperty ("dark", screen->isDark());
                o->setProperty ("three", screen->isThree() || screen->threeNote().isNotEmpty());     // (asked for, even if it could not start this time)
                const auto e = screen->eye();
                auto* v = new juce::DynamicObject();
                v->setProperty ("view", e.pose.view); v->setProperty ("turn", e.pose.turn); v->setProperty ("tilt", e.pose.tilt); v->setProperty ("away", e.pose.away);
                v->setProperty ("x", e.pose.tx); v->setProperty ("y", e.pose.ty); v->setProperty ("z", e.pose.tz);
                v->setProperty ("locked", e.locked); v->setProperty ("follow", e.follow);
                o->setProperty ("eye", juce::var (v));
                o->setProperty ("glow_hue", screen->glowHueNow()); o->setProperty ("glow_bright", screen->glowBrightNow()); o->setProperty ("glow_pulse", screen->glowPulseNow());
                o->setProperty ("load_lane", screen->loadFolder (false)); o->setProperty ("load_fx", screen->loadFolder (true));
                lookFile().replaceWithText (juce::JSON::toString (juce::var (o)));
                // if the 3D stage could not start, why is kept beside the session
                if (screen->threeNote().isNotEmpty())
                    lookFile().getSiblingFile ("3d.log").appendText (juce::Time::getCurrentTime().toString (true, true) + "  " + screen->threeNote() + "\n");
            };
            stageGuard.note = lookFile().getSiblingFile ("3d-starting");
            const bool died = stageGuard.lastStartDied();
            stageGuard.settled();
            screen->onThreeStarting = [this] { stageGuard.starting(); };
            screen->onThreeSettled = [this] { stageGuard.settled(); };
            if (three && died)
            {
                const juce::String why = "the 3D stage did not finish starting last time: it is left off (3 tries it again)";
                lookFile().getSiblingFile ("3d.log").appendText (juce::Time::getCurrentTime().toString (true, true) + "  " + why + "\n");
                screen->sayOnBar ("3D DID NOT FINISH STARTING LAST TIME: LEFT OFF.  3 TRIES IT AGAIN");
            }
            else if (three) screen->setThree (true);
        }
        window = std::make_unique<MainWindow> (screen);
        screen->grabKeyboardFocus();
        if (session->notSavingWhy.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "NOTHING WILL BE SAVED THIS TIME", session->notSavingWhy);
    }

    void shutdown() override
    {
        stageGuard.settled();               // (leaving in good order, even with the stage still coming up, is not a start that died)
        recorder = nullptr;                 // a take under way is finished and kept
        if (engine != nullptr)
        {
            if (auto xml = devices.createStateXml()) { audioFile().getParentDirectory().createDirectory(); xml->writeTo (audioFile()); }
            devices.removeAudioCallback (engine.get());
            devices.closeAudioDevice();
        }
        juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
        setup.deleteAndZero();              // the setup dialog, if it is still up: it reads the MIDI object
        onlineDialog.deleteAndZero();
        window = nullptr;
        midi = nullptr;
        online = nullptr;                   // before the session it reads: a send under way finishes or times out
        session = nullptr;
        engine = nullptr;
        sf0::Screen::releaseFonts();        // while JUCE is still up (left to the end of the process they abort the quit)
    }

    void systemRequestedQuit() override { quit(); }

private:
    static juce::File audioFile() { return sf0::Session::stateFile().getSiblingFile ("audio.xml"); }
    static juce::File lookFile() { return sf0::Session::stateFile().getSiblingFile ("look.json"); }

    void audioSetup()
    {
        if (setup != nullptr) { setup->toFront (true); return; }
        // Set once, before the set: which interface, which outputs, the buffer size.
        auto* sel = new SetupPanel (devices, *engine, *midi);
        if (auto* screen = window != nullptr ? dynamic_cast<sf0::Screen*> (window->getContentComponent()) : nullptr)
        {
            sel->setGlow (screen->glowHueNow(), screen->glowBrightNow(), screen->glowPulseNow());
            sel->onGlow = [screen] (float hue, float bright, float pulse) { screen->setGlow (hue, bright, pulse); if (screen->onLook) screen->onLook(); };
            sel->onFull = [screen] { if (screen->onFullScreen) screen->onFullScreen(); };
        }
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "SF-000 SETUP";
        o.dialogBackgroundColour = juce::Colour (0xff2B2B28);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        setup = o.launchAsync();
    }

    void onlineSetup()
    {
        if (onlineDialog != nullptr) { onlineDialog->toFront (true); return; }
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new OnlinePanel (*online));
        o.dialogTitle = "SF-000 ONLINE";
        o.dialogBackgroundColour = juce::Colour (0xff2B2B28);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        onlineDialog = o.launchAsync();
    }

    juce::AudioDeviceManager devices;
    std::unique_ptr<sf0::Engine> engine;
    std::unique_ptr<sf0::Session> session;
    std::unique_ptr<sf0::Online> online;
    juce::Component::SafePointer<juce::DialogWindow> onlineDialog;
    std::unique_ptr<sf0::Midi> midi;
    std::unique_ptr<sf0::Recorder> recorder;
    std::unique_ptr<MainWindow> window;
    StageGuard stageGuard;
    juce::Component::SafePointer<juce::DialogWindow> setup;
};

START_JUCE_APPLICATION (SF000App)
