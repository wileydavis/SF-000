// SF-000: the app. A window on the screens, the audio device, and three command-line tools:
//   SF-000 --test [--wav file]                 offline audio and timing checks
//   SF-000 --shot file.png [--page library|macros] [--lane N] [--scale 2]   draw a screen to a PNG
//   SF-000 --dump                              every hosted module's parameters and presets
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/Processor.h"
#include "core/Registry.h"
#include "Screen.h"

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

    // The kick alone: its first hit lands on the same sample whatever the block size.
    int on[4] = { -1, -1, -1, -1 }; const int blocks[4] = { 64, 100, 512, 1024 };
    for (int k = 0; k < 4; ++k) run (blocks[k], 1, nullptr, 0, nullptr, nullptr, nullptr, &on[k]);
    std::printf ("  kick's first hit at sample %d / %d / %d / %d (blocks of 64, 100, 512, 1024)\n", on[0], on[1], on[2], on[3]);
    for (int k = 1; k < 4; ++k) if (on[k] != on[0] || on[0] < 0) { std::printf ("  FAIL: timing depends on the block size\n"); ++bad; break; }

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
    check (std::abs (ses.pattern (7).steps[0].gate - 1.0f) < 0.01f, "a gate dragged right stops at the next lit step");
    click (stepKey (1)); click (stepKey (1));     // step 2 out again
    draw();
    dragBy (stepKey (0), 135.0f, 0.0f);
    check (ses.pattern (7).steps[0].gate > 2.5f, "with that step out, the gate runs on as a tie");
    dragBy (stepKey (9), 0.0f, -30.0f);
    check (ses.pattern (7).steps[9].vel > 0.95f, "a step dragged up gets louder");

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
    audio (200);
    draw();
    bool finite = true;
    for (int i = 0; i < 512; ++i) finite = finite && std::isfinite (b.getSample (0, i)) && std::isfinite (b.getSample (1, i));
    check (true, (juce::String (clicks) + " controls clicked, dragged and double-clicked across three screens").toRawUTF8());
    check (finite && ! e.fault.load(), "the audio is still clean afterwards");

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
    Screen sc (ses);
    const auto page = arg ("--page", "main");
    sc.setPage (page == "library" ? Screen::pLibrary : page == "macros" ? Screen::pMacros : Screen::pMain);
    const float scale = (float) juce::jlimit (0.25, 4.0, arg ("--scale", "2").getDoubleValue());
    sc.setSize ((int) Screen::kW, (int) Screen::kH);
    const auto img = sc.createComponentSnapshot (sc.getLocalBounds(), false, scale);
    juce::File f (arg ("--shot", "sf000.png"));
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (img, out);
    std::printf (ok ? "wrote %s (%d x %d)\n" : "could not write %s\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok ? 0 : 1;
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
        if (args.contains ("--shot")) { setApplicationReturnValue (shot (args)); quit(); return; }
        if (args.contains ("--audit")) { const int i = args.indexOf ("--audit"); setApplicationReturnValue (knobAudit (i + 1 < args.size() ? args[i + 1].getIntValue() - 1 : -1)); quit(); return; }
        if (args.contains ("--uitest")) { setApplicationReturnValue (uiTest()); quit(); return; }

        engine = std::make_unique<sf0::Engine>();
        session = std::make_unique<sf0::Session> (*engine);
        std::unique_ptr<juce::XmlElement> saved (juce::XmlDocument::parse (audioFile()));
        const auto err = devices.initialise (0, 2, saved.get(), true);
        if (err.isNotEmpty()) juce::Logger::writeToLog ("SF-000 audio: " + err);
        devices.addAudioCallback (engine.get());

        auto* screen = new sf0::Screen (*session);
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
        if (engine != nullptr)
        {
            if (auto xml = devices.createStateXml()) { audioFile().getParentDirectory().createDirectory(); xml->writeTo (audioFile()); }
            devices.removeAudioCallback (engine.get());
            devices.closeAudioDevice();
        }
        juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
        window = nullptr;
        session = nullptr;
        engine = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    static juce::File audioFile() { return sf0::Session::stateFile().getSiblingFile ("audio.xml"); }

    void audioSetup()
    {
        // Set once, before the set: which interface, which outputs, the buffer size.
        auto* sel = new juce::AudioDeviceSelectorComponent (devices, 0, 0, 2, 2, false, false, true, false);
        sel->setSize (520, 380);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "SF-000 AUDIO";
        o.dialogBackgroundColour = juce::Colour (0xff2B2B28);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    juce::AudioDeviceManager devices;
    std::unique_ptr<sf0::Engine> engine;
    std::unique_ptr<sf0::Session> session;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (SF000App)
