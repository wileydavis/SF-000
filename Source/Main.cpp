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
static int selfTest (const juce::String& wavPath)
{
    using namespace sf0;
    int bad = 0;
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
        return e.latencySamples();
    };

    std::printf ("SF-000 offline checks\n");
    std::vector<float> audio;
    float lufs = 0, peak = 0; std::array<double, kLanes> rms {};
    const int lat = run (512, 16, &audio, -1, &lufs, &peak, &rms, nullptr);
    std::printf ("  full rig, 16 bars at 132: short-term %.1f LUFS, peak %.1f dBFS, lane latency %d samples\n", lufs, peak, lat);
    for (int l = 0; l < kLanes; ++l)
    {
        std::printf ("  lane %2d %-10s %6.1f dB RMS%s\n", l + 1, lanes()[(size_t) l].name, rms[(size_t) l], rms[(size_t) l] < -70.0 ? "   SILENT" : "");
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
            setApplicationReturnValue (selfTest (w >= 0 && w + 1 < args.size() ? args[w + 1].unquoted() : juce::String()));
            quit(); return;
        }
        if (args.contains ("--shot")) { setApplicationReturnValue (shot (args)); quit(); return; }

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
