// SF-000: the app. A window on the screens, the audio device, and three command-line tools:
//   SF-000 --test [--wav file]                 offline audio and timing checks
//   SF-000 --shot file.png [--page library|macros] [--lane N] [--cfg lane|insert|return|space] [--space conv] [--scale 2]   draw a screen to a PNG
//   SF-000 --dump                              every hosted module's parameters and presets
//   SF-000 --e16 file.oxie16                   write the OXI E16 scene (a page per lane, the snapshot page)
//   SF-000 --miditest                          notes in, the E16's knobs and rings, the clock out: offline checks
//   SF-000 --perf                              the engine's share of real time and what the screen costs, on this machine
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/Processor.h"
#include "core/Registry.h"
#include "Screen.h"
#include "Midi.h"
#include "Recorder.h"
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

    // 2c. a part of the screen drawn on its own leaves the picture exactly as drawing all of it would
    {
        Engine ep; ep.prepare (48000.0, 512);
        Session sp (ep, false);
        Screen scp (sp);
        scp.setSize ((int) Screen::kW, (int) Screen::kH);
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
                for (int k = 0; k < a.getWidth() * x.pixelStride && ! off; ++k) off = std::abs ((int) p[k] - (int) q[k]) > 6;
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
        check (allSame && hitsKept && parts >= 12, (juce::String (parts) + " parts of the screen drawn on their own (knobs, the meters, the steps), with CONFIG shut and open: the same picture a whole redraw gives").toRawUTF8());
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
    // --rec: a take under way (in a folder of the system's temporary ones, gone again afterwards)
    const auto recDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_shot_rec");
    Recorder rec (e);
    rec.root = recDir;
    sc.recorder = &rec;
    if (args.contains ("--rec")) { rec.start(); for (int i = 0; i < 94 * 75; ++i) e.render (b); }
    const float scale = (float) juce::jlimit (0.25, 4.0, arg ("--scale", "2").getDoubleValue());
    sc.setSize ((int) Screen::kW, (int) Screen::kH);
    const auto img = sc.createComponentSnapshot (sc.getLocalBounds(), false, scale);
    rec.stop(); sc.recorder = nullptr; recDir.deleteRecursively();
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
    return ok ? 0 : 1;
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
        std::set<int> told;
        for (auto& m : out) if (m.isController()) told.insert (m.getChannel() * 128 + m.getControllerNumber());
        check ((int) told.size() == e16::kSlots && (int) out.size() == e16::kSlots, "a fresh E16 is told every value once (" + juce::String ((int) out.size()) + " messages)");
        out.clear();

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
        const double whole = frame (sc.getLocalBounds(), 30), master = frame ({ 1151, 782, 554, 190 }, 200), knob = frame ({ 300, 150, 76, 56 }, 200),
                     none = frame ({ 0, 0, 1, 1 }, 200);
        std::printf ("  the screen (drawn here in software at twice the size): the whole of it %.2f ms, the meters %.2f ms, one knob %.2f ms, nothing %.3f ms\n", whole, master, knob, none);
    }
    return 0;
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
        : audio (dm, 0, 0, 2, 2, false, false, true, false), engine (e), midi (m)
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
        setSize (520, 750);
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
        late.setBounds (r);
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
};

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

class SF000App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "SF-000"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& cmd) override
    {
        const auto args = juce::StringArray::fromTokens (cmd, true);
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
        if (args.contains ("--miditest")) { setApplicationReturnValue (midiTest()); quit(); return; }
        if (args.contains ("--perf")) { setApplicationReturnValue (perf()); quit(); return; }
        if (args.contains ("--e16")) { const int i = args.indexOf ("--e16"); setApplicationReturnValue (writeScene (i + 1 < args.size() ? args[i + 1].unquoted() : juce::String ("SF-000.oxie16"))); quit(); return; }

        engine = std::make_unique<sf0::Engine>();
        session = std::make_unique<sf0::Session> (*engine);
        std::unique_ptr<juce::XmlElement> saved (juce::XmlDocument::parse (audioFile()));
        const auto err = devices.initialise (0, 2, saved.get(), true);
        if (err.isNotEmpty()) juce::Logger::writeToLog ("SF-000 audio: " + err);
        devices.addAudioCallback (engine.get());
        midi = std::make_unique<sf0::Midi> (*engine, *session);
        recorder = std::make_unique<sf0::Recorder> (*engine);
        recorder->stateAtStart = [this] { session->save(); return sf0::Session::stateFile(); };

        auto* screen = new sf0::Screen (*session);
        screen->recorder = recorder.get();
        screen->onAudioSetup = [this] { audioSetup(); };
        screen->onFullScreen = [this]
        {
            auto& d = juce::Desktop::getInstance();
            d.setKioskModeComponent (d.getKioskModeComponent() == nullptr ? window.get() : nullptr, false);
        };
        window = std::make_unique<MainWindow> (screen);
        screen->grabKeyboardFocus();
    }

    void shutdown() override
    {
        recorder = nullptr;                 // a take under way is finished and kept
        if (engine != nullptr)
        {
            if (auto xml = devices.createStateXml()) { audioFile().getParentDirectory().createDirectory(); xml->writeTo (audioFile()); }
            devices.removeAudioCallback (engine.get());
            devices.closeAudioDevice();
        }
        juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
        setup.deleteAndZero();              // the setup dialog, if it is still up: it reads the MIDI object
        window = nullptr;
        midi = nullptr;
        session = nullptr;
        engine = nullptr;
        sf0::Screen::releaseFonts();        // while JUCE is still up (left to the end of the process they abort the quit)
    }

    void systemRequestedQuit() override { quit(); }

private:
    static juce::File audioFile() { return sf0::Session::stateFile().getSiblingFile ("audio.xml"); }

    void audioSetup()
    {
        if (setup != nullptr) { setup->toFront (true); return; }
        // Set once, before the set: which interface, which outputs, the buffer size.
        auto* sel = new SetupPanel (devices, *engine, *midi);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "SF-000 AUDIO AND MIDI";
        o.dialogBackgroundColour = juce::Colour (0xff2B2B28);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        setup = o.launchAsync();
    }

    juce::AudioDeviceManager devices;
    std::unique_ptr<sf0::Engine> engine;
    std::unique_ptr<sf0::Session> session;
    std::unique_ptr<sf0::Midi> midi;
    std::unique_ptr<sf0::Recorder> recorder;
    std::unique_ptr<MainWindow> window;
    juce::Component::SafePointer<juce::DialogWindow> setup;
};

START_JUCE_APPLICATION (SF000App)
