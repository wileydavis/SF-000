// SF-000: the app. A window on the screens, the audio device, and three command-line tools:
//   SF-000 --test [--wav file]                 offline audio and timing checks
//   SF-000 --shot file.png [--page library|macros] [--lane N] [--cfg lane|insert|return|space] [--space conv] [--scale 2] [--flat|--dark] [--info [--point x,y]] [--phones]   draw a screen to a PNG
//   SF-000 --dump                              every hosted module's parameters and presets
//   SF-000 --e16 file.oxie16                   write the OXI E16 scene (a page per lane, the snapshot page)
//   SF-000 --miditest                          notes in, the E16's knobs and rings, the clock out: offline checks
//   SF-000 --perf                              the engine's share of real time and what the screen costs, on this machine
#include <functional>
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
// POCKETS, the master's tool for who has which part of the spectrum: first on made-up sounds (a
// tone against a tone, a kick against a bass), where what it should do can be said in numbers,
// then in the rig.
static void pocketsTest (int& bad)
{
    using namespace sf0;
    const double SR = 48000.0;
    auto dbv = [] (double a) { return 20.0 * std::log10 (a + 1.0e-20); };
    // Thirteen parts of made-up sound, through the unit as the engine runs it.
    struct Bench
    {
        Pockets pk; Pockets::Set set;
        std::vector<std::vector<float>> ch;         // two for each lane, then the returns' four
        int block;
        explicit Bench (int blockSize = 256) : block (blockSize)
        {
            pk.prepare (48000.0, block);
            for (int l = 0; l < kLanes; ++l)
            {
                const auto& d = pocketDefault (l);
                set.home[(size_t) l] = Pockets::homeValue (d.homeHz); set.width[(size_t) l] = Pockets::widthValue (d.widthOct);
                set.hold[(size_t) l] = d.hold; set.yield[(size_t) l] = d.yield;
            }
            set.amount = 1.0f; set.clear = 6.0f / 18.0f; set.speed = 0.5f; set.returns = 0.5f;
            ch.assign ((size_t) (2 * kLanes + 4), std::vector<float> ((size_t) block, 0.0f));
        }
        void pocket (int lane, float hz, float oct, float hold, float yield)
        {
            set.home[(size_t) lane] = Pockets::homeValue (hz); set.width[(size_t) lane] = Pockets::widthValue (oct);
            set.hold[(size_t) lane] = hold; set.yield[(size_t) lane] = yield;
        }
        void silence() { for (auto& c : ch) std::fill (c.begin(), c.end(), 0.0f); }
        void put (int part, int i, float v) { ch[(size_t) (2 * part)][(size_t) i] += v; ch[(size_t) (2 * part + 1)][(size_t) i] += v; }
        void run (bool watch = false)
        {
            float* p[2 * kLanes + 4];
            for (size_t i = 0; i < ch.size(); ++i) p[i] = ch[i].data();
            pk.lanes (p, block, set, watch);
            pk.returns (p + 2 * kLanes, block);
        }
    };
    enum { A = 7, B = 9, RA = kLanes };             // the one with the claim (the LEAD's place), the one that gives (the PAD's), return A
    const float amp = std::pow (10.0f, -12.0f / 20.0f);
    // A tone at 1 kHz on A; on B (or on return A) a tone at 1 kHz and one at 4 kHz, all three as loud.
    // Run for `secs`, A sounding for the first `aFor` of them. What B's two tones are at the end
    // (in dB against what went in), over the last 80 ms; and whether B came out exactly as it went in.
    struct Tones { double at1k = 0.0, at4k = 0.0; bool exact = false; float cut1k = 0.0f; };
    auto tones = [&] (Bench& r, double secs, double aFor, int into = B, std::vector<float>* cutOverTime = nullptr)
    {
        std::vector<float> out;
        const int blocks = (int) (secs * SR / r.block);
        double pa = 0.0, p1 = 0.3, p4 = 0.7;
        Tones t;
        for (int blk = 0; blk < blocks; ++blk)
        {
            r.silence();
            std::vector<float> went ((size_t) r.block);
            for (int i = 0; i < r.block; ++i)
            {
                if ((blk * r.block + i) / SR < aFor) r.put (A, i, amp * (float) std::sin (pa));
                const float b = amp * (float) (std::sin (p1) + std::sin (p4));
                r.put (into, i, b); went[(size_t) i] = b;
                pa += juce::MathConstants<double>::twoPi * 1000.0 / SR; p1 += juce::MathConstants<double>::twoPi * 1000.0 / SR; p4 += juce::MathConstants<double>::twoPi * 4000.0 / SR;
            }
            r.run();
            const auto& got = r.ch[(size_t) (2 * into)];
            out.insert (out.end(), got.begin(), got.end());
            if (blk == blocks - 1) { t.exact = true; for (int i = 0; i < r.block; ++i) t.exact = t.exact && got[(size_t) i] == went[(size_t) i]; }
            if (cutOverTime != nullptr) cutOverTime->push_back (r.pk.cutNow (into, 15));
        }
        const int N = 3840, from = (int) out.size() - N;
        auto tone = [&] (double hz) { double re = 0.0, im = 0.0; for (int i = 0; i < N; ++i) { const double w = juce::MathConstants<double>::twoPi * hz * (double) (from + i) / SR; re += out[(size_t) (from + i)] * std::cos (w); im += out[(size_t) (from + i)] * std::sin (w); } return 2.0 * std::sqrt (re * re + im * im) / (double) N; };
        t.at1k = dbv (tone (1000.0)) - dbv (amp); t.at4k = dbv (tone (4000.0)) - dbv (amp);
        t.cut1k = r.pk.cutNow (into, 15);
        return t;
    };
    // (A's pocket is round 1 kHz and firmly held; B's is elsewhere, and B gives all it is asked)
    struct Two : Bench { Two() { pocket (A, 1000.0f, 1.0f, 1.0f, 0.0f); pocket (B, 300.0f, 1.0f, 0.5f, 1.0f); } };

    // Where two sounds meet, the one with the weaker claim sits CLEAR below the other, there and nowhere else.
    {
        Two r;
        const auto t = tones (r, 3.0, 9.0);
        const bool ok = std::abs (t.at1k + 6.0) < 0.8 && std::abs (t.at4k) < 0.3;
        std::printf ("  %s: POCKETS: a tone in another's pocket, as loud as the other's, is taken down to sit CLEAR below it (%.1f dB, CLEAR at 6), and the same sound's tone two octaves up is left alone (%+.2f dB)\n",
                     ok ? "ok  " : "FAIL", t.at1k, t.at4k);
        if (! ok) ++bad;
    }
    // Only while the other is there: once it stops, what gave way comes back, and is then exactly what went in.
    {
        Two r;
        std::vector<float> cut;
        const auto t = tones (r, 4.0, 1.5, B, &cut);
        const double perBlock = 256.0 / SR;
        int back = -1;
        for (int i = (int) (1.5 / perBlock); i < (int) cut.size() && back < 0; ++i) if (cut[(size_t) i] < 0.5f) back = i;
        const double backMs = back < 0 ? -1.0 : ((double) back * perBlock - 1.5) * 1000.0;
        const bool ok = t.exact && std::abs (t.at1k) < 0.01 && backMs > 50.0 && backMs < 900.0;
        std::printf ("  %s: the other gone, it comes back (within half a dB %.0f ms after), and is then exactly the sound that went in: no bell is left in its way\n", ok ? "ok  " : "FAIL", backMs);
        if (! ok) ++bad;
    }
    // AMOUNT, CLEAR, YIELD, HOLD and SPEED each do what they say.
    {
        Two half; half.set.amount = 0.5f;
        Two wide; wide.set.clear = 12.0f / 18.0f;
        Two mean; mean.set.yield[(size_t) B] = 3.0f / Pockets::kMaxCut;
        Two swap; swap.pocket (A, 300.0f, 1.0f, 0.5f, 1.0f); swap.pocket (B, 1000.0f, 1.0f, 1.0f, 0.0f);
        Two even; even.pocket (B, 1000.0f, 1.0f, 1.0f, 1.0f);
        const auto th = tones (half, 3.0, 9.0), tw = tones (wide, 3.0, 9.0), tmn = tones (mean, 3.0, 9.0), ts = tones (swap, 3.0, 9.0), te = tones (even, 3.0, 9.0);
        const float swapCutA = swap.pk.cutNow (A, 15);
        auto backAfter = [&] (float speed)
        {
            Two r; r.set.speed = speed;
            std::vector<float> cut; tones (r, 3.5, 1.5, B, &cut);
            const double perBlock = 256.0 / SR;
            for (int i = (int) (1.5 / perBlock); i < (int) cut.size(); ++i) if (cut[(size_t) i] < 0.5f) return ((double) i * perBlock - 1.5) * 1000.0;
            return 1.0e9;
        };
        const double slow = backAfter (0.0f), fast = backAfter (1.0f);
        const bool ok = std::abs (th.at1k + 3.0) < 0.6 && std::abs (tw.at1k + 12.0) < 1.2 && std::abs (tmn.at1k + 3.0) < 0.5
                     && std::abs (ts.at1k) < 0.05 && swapCutA > 4.0f && std::abs (te.at1k) < 0.05 && even.pk.cutNow (A, 15) == 0.0f
                     && fast < 200.0 && slow > 3.0 * fast;
        std::printf ("  %s: AMOUNT at half takes half of it (%.1f dB); CLEAR at 12 asks 12 (%.1f); YIELD at 3 dB gives 3 (%.1f); with the claims the other way round the other gives (%.1f dB on it, %+.2f on this); "
                     "two with the same claim to a band leave one another alone (%+.2f); SPEED sets how soon it is back (%.0f ms at full, %.0f at nought)\n",
                     ok ? "ok  " : "FAIL", th.at1k, tw.at1k, tmn.at1k, -swapCutA, ts.at1k, te.at1k, fast, slow);
        if (! ok) ++bad;
    }
    // The returns have no pocket: echo and reverb give way to a lane, by as much as RETURNS lets them.
    {
        Two r; r.set.returns = 0.25f;                               // 6 dB at most
        Two deep; deep.set.returns = 1.0f; deep.set.clear = 1.0f;
        const auto t = tones (r, 3.0, 9.0, RA), td = tones (deep, 3.0, 9.0, RA);
        Two none; none.set.returns = 0.0f;
        const auto tn = tones (none, 3.0, 9.0, RA);
        const bool ok = std::abs (t.at1k + 6.0) < 0.8 && std::abs (t.at4k) < 0.3 && td.at1k < -15.0 && tn.exact;
        std::printf ("  %s: a return gives way to a lane where the lane is sounding (%.1f dB with RETURNS at 6 and CLEAR at 6; %.1f with both at full), is left alone elsewhere (%+.2f dB), and with RETURNS at nought is not touched\n",
                     ok ? "ok  " : "FAIL", t.at1k, td.at1k, t.at4k);
        if (! ok) ++bad;
    }
    // A sound is taken to be where it is, not where the bank's bands overlap: a tone at 1 kHz makes room at 1 kHz.
    {
        Two r; r.set.clear = 1.0f; r.pocket (A, 1000.0f, 6.0f, 1.0f, 0.0f);       // (a pocket six octaves wide: only where it sounds can limit it)
        juce::Random rnd (11);
        double pa = 0.0;
        for (int blk = 0; blk < (int) (3.0 * SR / r.block); ++blk)
        {
            r.silence();
            for (int i = 0; i < r.block; ++i) { r.put (A, i, 0.05f * (float) std::sin (pa)); pa += juce::MathConstants<double>::twoPi * 1000.0 / SR; r.put (B, i, 0.5f * (rnd.nextFloat() * 2.0f - 1.0f)); }
            r.run();
        }
        float at = r.pk.cutNow (B, 15), far = 0.0f;
        for (int b = 0; b < Pockets::kBands; ++b) if (std::abs (b - 15) >= 3) far = std::max (far, r.pk.cutNow (B, b));
        const bool ok = at > 6.0f && far < 0.3f;
        std::printf ("  %s: a tone makes room where it is and not beside it: noise under a 1 kHz tone is cut %.1f dB at 1 kHz, and by no more than %.2f dB an octave or more away\n", ok ? "ok  " : "FAIL", at, far);
        if (! ok) ++bad;
    }
    // A kick against a bass. The bottom bands take tens of milliseconds to ring up, and the kick's
    // hardest part is over by then: once a kick has been heard, the next is known by its spectrum
    // from its first millisecond.
    {
        Bench r (64);
        const long per = (long) (0.4545 * SR);
        const float ab = std::pow (10.0f, -10.0f / 20.0f);
        double pb = 0.0; long t = 0;
        std::vector<float> cut;                 // the bass's cut round its own note, every 64 samples
        for (int blk = 0; blk < (int) (6 * per / 64); ++blk)
        {
            r.silence();
            for (int i = 0; i < 64; ++i, ++t)
            {
                const double tt = (double) (t % per) / SR;
                r.put (0, i, 0.7f * (float) (std::exp (-tt / 0.09) * std::sin (juce::MathConstants<double>::twoPi * (50.0 * tt + 60.0 * 0.02 * (1.0 - std::exp (-tt / 0.02))))));
                r.put (5, i, ab * (float) std::sin (pb)); pb += juce::MathConstants<double>::twoPi * 50.0 / SR;
            }
            r.run();
            cut.push_back (r.pk.cutNow (5, 2) + 0.27f * (r.pk.cutNow (5, 1) + r.pk.cutNow (5, 3)));
        }
        const int perB = (int) (per / 64);
        auto at = [&] (int hit, double ms) { return cut[(size_t) (hit * perB + (int) (ms * SR / 64000.0))]; };
        float deepest = 0.0f; for (int k = 0; k < perB; ++k) deepest = std::max (deepest, cut[(size_t) (4 * perB + k)]);
        const float first8 = at (0, 8.0), later8 = at (4, 8.0), before = at (4, -4.0);
        const bool ok = deepest > 3.0f && later8 > 0.5f * deepest && first8 < 0.2f * deepest && before < 0.45f * deepest;
        std::printf ("  %s: a bass under a kick: 8 ms into the fifth kick the bass's 50 Hz is down %.1f dB of the %.1f it reaches (the first kick, not yet known: %.1f dB by then), and it is back to %.1f before the next\n",
                     ok ? "ok  " : "FAIL", later8, deepest, first8, before);
        if (! ok) ++bad;
    }
    // What it costs, at its worst: thirteen parts of noise, every one of them giving way all over.
    {
        Bench r; juce::Random rnd (3);
        for (int l = 0; l < kLanes; ++l) r.set.yield[(size_t) l] = 1.0f;
        r.set.clear = 1.0f; r.set.returns = 1.0f;
        std::vector<std::vector<float>> noise = r.ch;
        for (auto& c : noise) for (auto& v : c) v = 0.2f * (rnd.nextFloat() * 2.0f - 1.0f);
        const int blocks = (int) (8.0 * SR / r.block);
        auto timeIt = [&] (float amount, bool watch)
        {
            r.set.amount = amount;
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int blk = 0; blk < blocks; ++blk) { r.ch = noise; r.run (watch); }
            return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / 8.0 * 100.0;
        };
        const double off = timeIt (0.0f, false), looking = timeIt (0.0f, true), full = timeIt (1.0f, false);
        int bells = 0; for (int q = 0; q < Pockets::kParts; ++q) for (int b = 0; b < Pockets::kBands; ++b) bells += r.pk.cutNow (q, b) > 0.0f ? 1 : 0;
        const bool ok = full - off < 12.0 && looking - off < 5.0;
        std::printf ("  %s: its cost: looking at all thirteen parts, %.1f %% of one core of this machine; with %d of its 364 bells cutting at once, %.1f %% (and none at all with AMOUNT at nought)\n",
                     ok ? "ok  " : "FAIL", looking - off, bells, full - off);
        if (! ok) ++bad;
    }
    // AUTO, on made-up spectra: each lane's pocket is put where its own sound is, and no wider than it takes to hold it.
    {
        Pockets::Seen sn;
        for (auto& row : sn.usual) row.fill (-120.0f);
        auto hump = [&] (int lane, float band, float halfWidth, float db) { for (int b = 0; b < Pockets::kBands; ++b) { const float d = ((float) b - band) / halfWidth; sn.usual[(size_t) lane][(size_t) b] = std::max (sn.usual[(size_t) lane][(size_t) b], db - 12.0f * d * d); } sn.heardFor[(size_t) lane] = 10.0f; };
        hump (0, 2.0f, 1.0f, -30.0f);                               // a kick at 50 Hz, with a click far up and far down
        hump (0, 21.0f, 2.0f, -52.0f);
        hump (1, 25.0f, 1.5f, -45.0f);                              // a hat at 10 kHz
        for (int b = 8; b <= 20; ++b) sn.usual[8][(size_t) b] = -44.0f;     // a chord spread evenly from 200 Hz to 3 kHz
        sn.heardFor[8] = 10.0f;
        sn.heardFor[9] = 0.5f; for (auto& v : sn.usual[9]) v = -40.0f;      // (a pad heard for half a second: too little to go by)
        Pockets::Set now, out; std::array<bool, kLanes> placed;
        for (int l = 0; l < kLanes; ++l) { const auto& d = pocketDefault (l); now.home[(size_t) l] = Pockets::homeValue (d.homeHz); now.width[(size_t) l] = Pockets::widthValue (d.widthOct); now.hold[(size_t) l] = d.hold; now.yield[(size_t) l] = d.yield; }
        Pockets::place (sn, now, out, placed);
        const float kick = Pockets::homeHz (out.home[0]), hat = Pockets::homeHz (out.home[1]), chord = Pockets::homeHz (out.home[8]);
        const float kickW = Pockets::widthOct (out.width[0]), chordW = Pockets::widthOct (out.width[8]);
        int n = 0; for (bool b : placed) n += b ? 1 : 0;
        const bool ok = n == 3 && placed[0] && placed[1] && placed[8] && ! placed[9] && kick > 44.0f && kick < 56.0f && hat > 9000.0f && hat < 11500.0f && chord > 550.0f && chord < 1050.0f
                     && kickW < 1.5f && chordW > 2.5f && out.home[9] == now.home[9] && out.hold == now.hold && out.yield == now.yield;
        std::printf ("  %s: AUTO, given a kick at 50 Hz, a hat at 10 kHz and a chord spread evenly from 200 Hz to 3 kHz: the kick's pocket goes to %.0f Hz and is %.1f octaves wide, the hat's to %.1f kHz, the chord's to %.0f Hz and %.1f octaves; a lane heard for half a second is left where it was\n",
                     ok ? "ok  " : "FAIL", kick, kickW, hat / 1000.0f, chord, chordW);
        if (! ok) ++bad;
    }

    // ---- in the rig
    struct Tap : Engine::RecSink
    {
        std::array<std::vector<float>, Engine::kRecTracks> t;
        void begin (int) noexcept override {}
        void track (int idx, const float* l, const float*, int n) noexcept override { auto& v = t[(size_t) idx]; if (v.size() + (size_t) n <= v.capacity()) v.insert (v.end(), l, l + n); }
        void end (int) noexcept override {}
    };
    using Sets = std::vector<std::pair<int, float>>;
    const int block = 256;
    bool worked = false, sound = true;
    std::array<double, Pockets::kParts> cutSeen {};
    auto run = [&] (const Sets& sets, std::vector<int> onlyLanes, bool watch, Tap& tap)
    {
        Engine e; e.prepare (SR, block);
        for (auto& kv : sets) e.set (kv.first, kv.second, 0.0);
        if (! onlyLanes.empty()) for (int l = 0; l < kLanes; ++l) if (std::find (onlyLanes.begin(), onlyLanes.end(), l) == onlyLanes.end()) e.set (cx::lane (l, cx::kMute), 1.0f, 0.0);
        e.watchPockets (watch);
        const double beat = 60.0 / e.tempo();
        for (auto& v : tap.t) { v.clear(); v.reserve ((size_t) (9.0 * beat * SR)); }
        juce::AudioBuffer<float> b (2, block);
        e.setRunning (true);
        for (int i = 0; i < (int) (8.0 * beat * SR / block); ++i) e.render (b);         // two bars to settle in
        e.setRecSink (&tap);
        cutSeen.fill (0.0);
        for (int i = 0; i < (int) (8.0 * beat * SR / block); ++i)
        {
            e.render (b);
            for (int q = 0; q < Pockets::kParts; ++q) for (int k = 0; k < Pockets::kBands; ++k) cutSeen[(size_t) q] = std::max (cutSeen[(size_t) q], (double) e.pocketsForTests().cutNow (q, k));
            for (int i2 = 0; i2 < block; ++i2) sound = sound && std::isfinite (b.getSample (0, i2));
        }
        e.setRecSink (nullptr);
        worked = e.pocketsForTests().working();
        sound = sound && ! e.fault.load();
    };
    auto same = [] (const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && ! a.empty() && std::equal (a.begin(), a.end(), b.begin()); };
    auto differ = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        double d = 0.0, sum = 0.0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) { d += (double) (a[i] - b[i]) * (a[i] - b[i]); sum += (double) a[i] * a[i]; }
        return 10.0 * std::log10 ((d + 1.0e-20) / (sum + 1.0e-20));
    };
    auto rms = [] (const std::vector<float>& a) { double s = 0.0; for (float v : a) s += (double) v * v; return 10.0 * std::log10 (s / (double) std::max<size_t> (1, a.size()) + 1.0e-20); };
    const int AMOUNT = cx::kPkG, CLEAR = cx::kPkG + 1, SPEED = cx::kPkG + 2, RETURNS = cx::kPkG + 3;
    auto plain = std::make_unique<Tap>(), watched = std::make_unique<Tap>(), full = std::make_unique<Tap>(), other = std::make_unique<Tap>();

    // With AMOUNT at nought it is not there: it does no work, and the set is the same to the last bit whether or not the page is looking.
    {
        run ({}, {}, false, *plain);
        const bool idle = ! worked;
        run ({}, {}, true, *watched);
        const bool looking = worked;
        bool every = true;
        for (int t = 0; t < Engine::kRecTracks; ++t) every = every && (plain->t[(size_t) t].empty() ? watched->t[(size_t) t].empty() : same (plain->t[(size_t) t], watched->t[(size_t) t]));
        const bool ok = idle && looking && every && plain->t[0].size() > 48000;
        std::printf ("  %s: in the rig, AMOUNT at nought (where it starts): POCKETS does no work at all, and with its page up and looking the set and every stem are the same to the last bit\n", ok ? "ok  " : "FAIL");
        if (! ok) ++bad;
    }
    // At work on the whole rig: nothing breaks, the kick is not touched, the others give where they should.
    {
        run ({ { AMOUNT, 1.0f } }, {}, false, *full);
        int gave = 0; double most = 0.0; juce::String who;
        for (int l = 1; l < kLanes; ++l) if (cutSeen[(size_t) l] > 1.0) { ++gave; if (cutSeen[(size_t) l] > most) { most = cutSeen[(size_t) l]; who = lanes()[(size_t) l].name; } }
        const double setWas = rms (plain->t[0]), setIs = rms (full->t[0]);
        double worst = 0.0; juce::String worstWho;
        for (int l = 0; l < kLanes; ++l) if (const double d = rms (plain->t[(size_t) (1 + l)]) - rms (full->t[(size_t) (1 + l)]); d > worst) { worst = d; worstWho = lanes()[(size_t) l].name; }
        const bool ok = sound && same (plain->t[1], full->t[1]) && cutSeen[0] == 0.0 && gave >= 4 && most <= Pockets::kMaxCut + 0.01 && std::abs (setWas - setIs) < 3.0 && worst < 5.0;
        std::printf ("  %s: AMOUNT at full, every lane playing: the kick's stem is the same to the last bit (its YIELD is nought), %d other lanes give somewhere (the most in one band: %.1f dB, %s), no lane loses more than %.1f dB over all (%s), and the set is %+.1f dB\n",
                     ok ? "ok  " : "FAIL", gave, most, who.toRawUTF8(), worst, worstWho.toRawUTF8(), setIs - setWas);
        if (! ok) ++bad;
    }
    // What is not heard covers nothing: the bass under the kick gives; the kick muted, the bass is as it came.
    {
        Tap a, b, c;
        run ({}, { 0, 5 }, false, a);
        run ({ { AMOUNT, 1.0f }, { CLEAR, 1.0f }, { cx::pk (5, 3), 1.0f } }, { 0, 5 }, false, b);
        const double gives = differ (a.t[6], b.t[6]), bassCut = cutSeen[5];
        run ({ { AMOUNT, 1.0f }, { CLEAR, 1.0f }, { cx::pk (5, 3), 1.0f } }, { 5 }, false, c);
        Tap alone; run ({}, { 5 }, false, alone);
        const bool ok = gives > -40.0 && bassCut > 3.0 && same (alone.t[6], c.t[6]);
        std::printf ("  %s: the kick and the bass alone: the bass gives under the kick (up to %.1f dB in a band; its stem differs by %.0f dB); with the kick muted the bass is the bass as it came, to the last bit\n",
                     ok ? "ok  " : "FAIL", bassCut, gives);
        if (! ok) ++bad;
    }
    // Every knob is heard: the rig's four, and a lane's four.
    {
        juce::String heard; bool all = true;
        auto pair = [&] (const char* name, const Sets& lo, const Sets& hi, int track)
        {
            Tap a, b;
            run (lo, {}, false, a); run (hi, {}, false, b);
            const double d = differ (a.t[(size_t) track], b.t[(size_t) track]);
            heard << (heard.isEmpty() ? "" : ", ") << name << " " << juce::String ((int) std::lround (d));
            all = all && d > -45.0;
        };
        pair ("AMOUNT", { { AMOUNT, 0.0f } }, { { AMOUNT, 1.0f } }, 0);
        pair ("CLEAR", { { AMOUNT, 1.0f }, { CLEAR, 0.0f } }, { { AMOUNT, 1.0f }, { CLEAR, 1.0f } }, 0);
        pair ("SPEED", { { AMOUNT, 1.0f }, { CLEAR, 1.0f }, { SPEED, 0.0f } }, { { AMOUNT, 1.0f }, { CLEAR, 1.0f }, { SPEED, 1.0f } }, 0);
        // (the returns, with the lead and the chord sent hard to them so there is something of theirs to cut)
        const Sets sends = { { AMOUNT, 1.0f }, { CLEAR, 1.0f }, { cx::lane (7, cx::kSendA), 1.0f }, { cx::lane (8, cx::kSendB), 1.0f }, { cx::lane (5, cx::kSendA), 0.8f } };
        Sets r0 = sends, r1 = sends; r0.push_back ({ RETURNS, 0.0f }); r1.push_back ({ RETURNS, 1.0f });
        pair ("RETURNS", r0, r1, 1 + kLanes);
        const Sets on = { { AMOUNT, 1.0f }, { CLEAR, 0.5f } };
        auto with = [&] (int knob, float v) { Sets x = on; x.push_back ({ cx::pk (5, knob), v }); return x; };
        pair ("BASS HOME", with (0, Pockets::homeValue (60.0f)), with (0, Pockets::homeValue (400.0f)), 6);
        pair ("WIDTH", with (1, 0.0f), with (1, 1.0f), 6);
        pair ("HOLD", with (2, 0.0f), with (2, 1.0f), 6);
        pair ("YIELD", with (3, 0.0f), with (3, 1.0f), 6);
        std::printf ("  %s: each of its knobs is heard, end to end of its travel: the rig's four, and a lane's four on the bass (dB of difference: %s)\n", all ? "ok  " : "FAIL", heard.toRawUTF8());
        if (! all) ++bad;
    }
    // A sweep of HOME travels: it is a knob like any other, and GLIDE carries it.
    {
        Engine e; e.prepare (SR, block);
        e.set (AMOUNT, 1.0f, 0.0);
        juce::AudioBuffer<float> b (2, block);
        e.setRunning (true);
        for (int i = 0; i < 200; ++i) e.render (b);
        const float from = e.shown (cx::pk (5, 0));
        e.set (cx::pk (5, 0), Pockets::homeValue (800.0f), 8.0);       // over two bars
        const double beat = 60.0 / e.tempo();
        for (int i = 0; i < (int) (4.5 * beat * SR / block); ++i) e.render (b);
        const float mid = e.shown (cx::pk (5, 0));
        for (int i = 0; i < (int) (5.0 * beat * SR / block); ++i) e.render (b);
        const float to = e.shown (cx::pk (5, 0));
        const bool ok = mid > from + 0.1f * (to - from) && mid < to - 0.1f * (to - from) && std::abs (Pockets::homeHz (to) - 800.0f) < 2.0f && e.valueText (cx::pk (5, 0)) == "800 HZ";
        std::printf ("  %s: the bass's HOME sent from %.0f Hz to 800 Hz over two bars is on its way half way through (%.0f Hz) and there at the end (%s)\n",
                     ok ? "ok  " : "FAIL", Pockets::homeHz (from), Pockets::homeHz (mid), e.valueText (cx::pk (5, 0)).toRawUTF8());
        if (! ok) ++bad;
    }
    // A snapshot holds the pockets; a recall brings them back; the session file keeps them by name;
    // a snapshot from before there were any leaves them where they are. AUTO and RESET.
    {
        using juce::var;
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_pockets_test");
        tmp.deleteRecursively(); tmp.createDirectory();
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        bool kept = false, back = false, left = false, scoped = false, inFile = false, madeSays = false, autoOk = false, resetOk = false, reaches = false, onDisk = false;
        juce::String autoSays;
        juce::int64 withId = 0;
        {
            Engine e; e.prepare (SR, block);
            Session ses (e, true);
            juce::AudioBuffer<float> b (2, block);
            auto bars = [&] (double n) { const int blocks = (int) (n * 4.0 * 60.0 / e.tempo() * SR / block); for (int k = 0; k < blocks; ++k) e.render (b); };
            ses.userSet (cx::kGlide, 0.0f);
            const float homeWas = e.target (cx::pk (5, 0));
            ses.userSet (AMOUNT, 0.7f); ses.userSet (cx::pk (5, 0), 0.33f); ses.userSet (cx::pk (9, 3), 0.9f);
            bars (0.1);
            withId = ses.take();
            const auto& snap = ses.library[(size_t) ses.indexOf (withId)];
            kept = std::abs (snap.values[(size_t) AMOUNT] - 0.7f) < 1.0e-6f && std::abs (snap.values[(size_t) cx::pk (5, 0)] - 0.33f) < 1.0e-6f;
            madeSays = snap.made["pockets"].toString().length() == 16 && snap.made["pockets"].toString() == e.defs().pockets;
            // a snapshot from before: no word of pockets in it
            Snapshot old = snap; old.id = withId + 77; old.name = "FROM BEFORE"; old.uid.clear(); old.kept = var();
            for (int i = 0; i < cx::kCount; ++i) if (cx::isPk (i)) old.values[(size_t) i] = -1.0f;
            ses.library.push_back (old);
            ses.userSet (AMOUNT, 0.2f); ses.userSet (cx::pk (5, 0), 0.6f); ses.userSet (cx::pk (9, 3), 0.1f);
            bars (0.1);
            ses.recall (old.id); bars (0.1);
            left = std::abs (e.target (AMOUNT) - 0.2f) < 1.0e-6f && std::abs (e.target (cx::pk (5, 0)) - 0.6f) < 1.0e-6f;
            // part of a snapshot: the bass alone brings the bass's pocket and not the rig's AMOUNT or the pad's
            Session::Scope sc; sc.lane.fill (false); sc.lane[5] = true; sc.returns = false; sc.bus = false; sc.master = false;
            ses.recall (withId, sc); bars (0.1);
            scoped = std::abs (e.target (cx::pk (5, 0)) - 0.33f) < 1.0e-6f && std::abs (e.target (AMOUNT) - 0.2f) < 1.0e-6f && std::abs (e.target (cx::pk (9, 3)) - 0.1f) < 1.0e-6f;
            ses.recall (withId); bars (0.1);
            back = std::abs (e.target (AMOUNT) - 0.7f) < 1.0e-6f && std::abs (e.target (cx::pk (5, 0)) - 0.33f) < 1.0e-6f && std::abs (e.target (cx::pk (9, 3)) - 0.9f) < 1.0e-6f;
            reaches = Session::reaches (e, AMOUNT) && Session::reaches (e, cx::pk (5, 0)) && ! Session::reaches (e, cx::kMaster);
            // RESET, then AUTO from what plays
            ses.pocketsReset(); bars (0.1);
            resetOk = std::abs (e.target (cx::pk (5, 0)) - homeWas) < 1.0e-6f && std::abs (e.target (cx::pk (9, 3)) - e.control (cx::pk (9, 3)).def) < 1.0e-6f && std::abs (e.target (AMOUNT) - 0.7f) < 1.0e-6f;
            const int none = ses.pocketsAuto();                                 // (nothing has been looked at: nothing is moved)
            e.watchPockets (true);
            e.setRunning (true);
            bars (6.0);
            const int n = ses.pocketsAuto(); bars (0.1);
            const float kick = Pockets::homeHz (e.target (cx::pk (0, 0))), hat = Pockets::homeHz (e.target (cx::pk (1, 0))), bass = Pockets::homeHz (e.target (cx::pk (5, 0))), clap = Pockets::homeHz (e.target (cx::pk (3, 0)));
            autoOk = none == 0 && n >= 9 && kick > 35.0f && kick < 110.0f && hat > 4000.0f && bass > 40.0f && bass < 500.0f && clap > 500.0f && clap < 5000.0f
                  && std::abs (e.target (cx::pk (5, 2)) - e.control (cx::pk (5, 2)).def) < 1.0e-6f;
            autoSays = juce::String (n) + " lanes placed: KICK " + juce::String ((int) kick) + " Hz, BASS " + juce::String ((int) bass) + " Hz, CLAP " + juce::String ((int) clap) + " Hz, CLOSED HAT " + juce::String (hat / 1000.0f, 1) + " kHz";

            e.setRunning (false);
            ses.recall (withId); bars (0.1);
            ses.waitForClips(); ses.save();
            const var root = juce::JSON::parse (Session::stateFile().loadFileAsString());
            for (int i = 0; i < root["library"].size(); ++i)
                if ((juce::int64) root["library"][i]["id"] == withId)
                    inFile = std::abs ((double) root["library"][i]["values"]["pk.amount"] - 0.7) < 1.0e-4 && std::abs ((double) root["library"][i]["values"]["bass.pk_home"] - 0.33) < 1.0e-4
                          && root["library"][i]["made"]["pockets"].toString().length() == 16 && root["defs"][juce::Identifier (root["library"][i]["made"]["pockets"].toString())]["kind"].toString() == "pockets";
        }
        {
            Engine e; e.prepare (SR, block);
            Session ses (e, true);
            onDisk = std::abs (e.target (AMOUNT) - 0.7f) < 1.0e-4f && std::abs (e.target (cx::pk (5, 0)) - 0.33f) < 1.0e-4f && ses.indexOf (withId) >= 0
                  && std::abs (ses.library[(size_t) ses.indexOf (withId)].values[(size_t) cx::pk (9, 3)] - 0.9f) < 1.0e-4f;
        }
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
        const bool ok = kept && madeSays && left && scoped && back && reaches && resetOk && autoOk && inFile && onDisk;
        std::printf ("  %s: a snapshot holds the pockets and a recall brings them back; one from before there were any leaves them as they are; the bass alone, recalled, brings the bass's and not the rig's; a snapshot knob can reach them; "
                     "they are in the session file by name with what they were made with, and there when it opens. RESET puts every pocket back; AUTO, after six bars: %s\n", ok ? "ok  " : "FAIL", autoSays.toRawUTF8());
        if (! ok) { std::printf ("        (kept %d made %d left %d scoped %d back %d reaches %d reset %d auto %d file %d disk %d)\n", kept, madeSays, left, scoped, back, reaches, resetOk, autoOk, inFile, onDisk); ++bad; }
    }
}

// MOTION on the snapshot knobs: a wave swings a snapshot knob in time with the clock, and what the
// knob moves goes with it, worked out on the audio thread.
static void knobMotionTest (int& bad)
{
    using namespace sf0;
    using juce::var;
    const double SR = 48000.0;
    const int block = 128;
    auto say = [&bad] (bool ok, const juce::String& what) { std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++bad; };
    const int W = 0, S = 1, K = 2, A = 3;       // a unit's four: WAVE, SPEED, KNOB, AMOUNT
    const auto waves = Engine::motionWaves();
    auto wave = [&] (const char* name) { return (float) waves.indexOf (name) / (float) (waves.size() - 1); };
    auto speed = [] (const char* name) { const auto all = Engine::motionSpeeds(); return (float) all.indexOf (name) / (float) (all.size() - 1); };
    auto knob = [] (int k) { return (float) k / 15.0f; };
    const int tune = cx::lane (5, 0), other = cx::lane (5, 3), far = cx::lane (9, 1);

    // 1. a knob tied to knobs: the wave swings it, what it is tied to goes with it, and nothing else moves
    {
        Engine e; e.prepare (SR, block);
        Session ses (e, false);
        juce::AudioBuffer<float> b (2, block);
        const double beat = 60.0 / e.tempo();
        const int perBar = (int) std::lround (4.0 * beat * SR / block);
        ses.userSet (cx::kGlide, 0.0f);
        const float base = e.target (tune), otherWas = e.target (other);
        ses.knobs[2].links = { { tune, 0.4f } };
        ses.userSet (cx::kSnap + 2, 0.5f);
        for (int i = 0; i < 40; ++i) e.render (b);
        const float stands = e.target (tune);
        const bool tied = std::abs (stands - (base + 0.2f)) < 1.0e-5f && Session::reaches (e, tune);
        // nothing yet: a wave chosen with no amount is no motion
        ses.userSet (cx::mot (0, W), wave ("SINE")); ses.userSet (cx::mot (0, K), knob (2));
        for (int i = 0; i < 40; ++i) e.render (b);
        const bool still = e.shown (tune) == stands && ses.motionOn (2) == 0 && e.motionPush (tune) == 0.0f;
        ses.userSet (cx::mot (0, A), 0.75f);                    // half the knob's turn, each way
        e.setRunning (true);
        float lo = 2.0f, hi = -1.0f, kLo = 2.0f, kHi = -1.0f, atBar = 9.0f, quarter = 0.0f;
        bool glides = false, othersMoved = false, targetMoved = false;
        std::vector<float> seen;
        for (int i = 0; i < perBar * 4; ++i)
        {
            const double p0 = e.songPos();
            e.render (b);
            const double p1 = e.songPos();
            const float v = e.shown (tune);
            seen.push_back (v);
            lo = std::min (lo, v); hi = std::max (hi, v);
            kLo = std::min (kLo, e.shown (cx::kSnap + 2)); kHi = std::max (kHi, e.shown (cx::kSnap + 2));
            glides = glides || e.isGliding (tune) || e.isGliding (cx::kSnap + 2);
            othersMoved = othersMoved || e.shown (other) != otherWas || e.shown (far) != e.target (far);
            targetMoved = targetMoved || e.target (tune) != stands || e.target (cx::kSnap + 2) != 0.5f;
            if (p0 > 3.9 && std::floor (p1 / 4.0) > std::floor (p0 / 4.0)) atBar = std::min (atBar, std::abs (v - stands));        // the block a bar line fell in
            if (p0 < 9.0 && p1 >= 9.0) quarter = v - stands;                                                                       // a quarter of the way through the third bar: the top
        }
        // a bar on, it is where it was
        float off = 0.0f;
        for (size_t i = 0; i + (size_t) perBar < seen.size(); ++i) off = std::max (off, std::abs (seen[i + (size_t) perBar] - seen[i]));
        const bool ok = tied && still && ses.motionOn (2) == 1 && std::abs (hi - (stands + 0.2f)) < 0.004f && std::abs (lo - (stands - 0.2f)) < 0.004f && kHi > 0.99f && kLo < 0.01f
                     && ! glides && ! othersMoved && ! targetMoved && atBar < 0.012f && std::abs (quarter - 0.2f) < 0.006f && off < 0.012f;
        say (ok, "MOTION on a knob tied to the bass's TUNE: a SINE a bar long, half the knob's turn each way, swings the knob from one end to the other and TUNE with it (" + juce::String (lo - stands, 3) + " to +" + juce::String (hi - stands, 3)
                     + " about where it stands; 0.2 is the tie's share), in time with the bar (" + juce::String (atBar, 4) + " off at a bar line, +" + juce::String (quarter, 3) + " a beat in, the same a bar on to " + juce::String (off, 4)
                     + "); where the knobs stand is not touched, nothing else moves, and nothing is said to be travelling");
        if (! ok) std::printf ("        (tied %d still %d on %d hi %.4f lo %.4f knob %.3f..%.3f glides %d others %d target %d)\n", tied, still, ses.motionOn (2), hi - stands, lo - stands, kLo, kHi, glides, othersMoved, targetMoved);

        // stopped, the wave carries on; AMOUNT the other way turns it over; two on one knob add; off, it is where it stood
        e.setRunning (false);
        float sLo = 2.0f, sHi = -1.0f;
        for (int i = 0; i < perBar * 2; ++i) { e.render (b); sLo = std::min (sLo, e.shown (tune)); sHi = std::max (sHi, e.shown (tune)); }
        e.setRunning (true);
        for (int i = 0; i < 8; ++i) e.render (b);
        auto pushAt = [&] (double beatInBar)
        {
            float got = 0.0f;
            for (int i = 0; i < perBar * 3; ++i)
            {
                const double p0 = std::fmod (e.songPos(), 4.0);
                e.render (b);
                const double p1 = std::fmod (e.songPos(), 4.0);
                if (p0 < beatInBar && p1 >= beatInBar) { got = e.shown (tune) - stands; break; }
            }
            return got;
        };
        ses.userSet (cx::mot (0, A), 0.25f);
        const float turned = pushAt (1.0);
        ses.userSet (cx::mot (0, A), 0.625f);                                                   // a quarter of the turn
        ses.userSet (cx::mot (1, W), wave ("SINE")); ses.userSet (cx::mot (1, K), knob (2)); ses.userSet (cx::mot (1, A), 0.625f);
        const float two = pushAt (1.0);
        ses.userSet (cx::mot (1, A), 0.5f);
        const float one = pushAt (1.0);
        ses.userSet (cx::mot (0, W), 0.0f);
        for (int i = 0; i < 40; ++i) e.render (b);
        const bool back = e.shown (tune) == stands && e.shown (cx::kSnap + 2) == 0.5f && ses.motionOn (2) == 0;
        const bool ok2 = sHi - sLo > 0.39f && std::abs (turned + 0.2f) < 0.006f && std::abs (one - 0.1f) < 0.006f && std::abs (two - 0.2f) < 0.006f && back;
        say (ok2, "stopped, the wave carries on (" + juce::String (sHi - sLo, 3) + " end to end); AMOUNT the other way turns it over (" + juce::String (turned, 3) + " a beat in); two on one knob add ("
                      + juce::String (one, 3) + " alone, " + juce::String (two, 3) + " together); and with the wave OFF everything is exactly where it stood");

        // 2. a knob of snapshots: the wave takes it through them as a hand would
        ses.userSet (cx::kSnap + 2, 0.0f);
        ses.knobs[2].links.clear();
        ses.userSet (tune, 0.20f); ses.userSet (other, 0.10f); ses.userSet (far, 0.30f);
        for (int i = 0; i < 20; ++i) e.render (b);
        const auto a = ses.take();
        ses.userSet (tune, 0.80f); ses.userSet (far, 0.50f);
        for (int i = 0; i < 20; ++i) e.render (b);
        const auto c = ses.take();
        ses.userSet (tune, 0.40f); ses.userSet (other, 0.90f);
        for (int i = 0; i < 20; ++i) e.render (b);
        const auto d = ses.take();
        ses.recall (a);
        for (int i = 0; i < 40; ++i) e.render (b);
        ses.knobs[4].snaps = { a, c, d };
        ses.knobs[4].depth = 1.0f;
        // by hand, at four places
        float hand[4][3];
        const float at[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
        for (int q = 0; q < 4; ++q) { ses.turnSnapKnob (4, at[q]); hand[q][0] = e.target (tune); hand[q][1] = e.target (other); hand[q][2] = e.target (far); }
        ses.turnSnapKnob (4, 0.0f);
        for (int i = 0; i < 40; ++i) e.render (b);
        // by the wave: a ramp, a bar long, the whole turn
        ses.userSet (cx::mot (2, W), wave ("RAMP UP")); ses.userSet (cx::mot (2, S), speed ("1 BAR")); ses.userSet (cx::mot (2, K), knob (4)); ses.userSet (cx::mot (2, A), 1.0f);
        float worst = 0.0f;
        int looked = 0;
        for (int i = 0; i < perBar * 3; ++i)
        {
            const double p0 = std::fmod (e.songPos(), 4.0);
            e.render (b);
            const double p1 = std::fmod (e.songPos(), 4.0);
            for (int q = 0; q < 3; ++q)                                                         // (the last place is the ramp's end, where it falls back: not looked at)
                if (p0 < at[q] * 4.0 && p1 >= at[q] * 4.0)
                {
                    // (where the wave is at the block's end, not at the line itself: the knob is that much further on)
                    const float ahead = (float) ((p1 - at[q] * 4.0) / 4.0);
                    const float slope[3] = { (hand[q + 1][0] - hand[q][0]) / 0.25f, (hand[q + 1][1] - hand[q][1]) / 0.25f, (hand[q + 1][2] - hand[q][2]) / 0.25f };
                    worst = std::max ({ worst, std::abs (e.shown (tune) - hand[q][0] - slope[0] * ahead), std::abs (e.shown (other) - hand[q][1] - slope[1] * ahead), std::abs (e.shown (far) - hand[q][2] - slope[2] * ahead) });
                    ++looked;
                }
        }
        const bool ok3 = looked >= 6 && worst < 0.01f && std::abs (hand[1][0] - 0.80f) < 1.0e-4f && std::abs (hand[3][1] - 0.90f) < 1.0e-4f && ses.motionOn (4) == 4;
        say (ok3, "on a knob of three snapshots, a RAMP UP over the whole turn takes TUNE, a second knob and the pad's through them exactly as the hand does: a quarter, a half and three quarters of the way round, each is where the knob turned there by hand puts it ("
                      + juce::String (worst, 4) + " off at most, over " + juce::String (looked) + " looks)");

        // a recall leaves MOTION as it is, and a snapshot taken with it running holds where the knobs stand, not where the wave has them
        const float standsNow = e.target (tune);
        const auto withWave = ses.take();
        const float held = ses.library[(size_t) ses.indexOf (withWave)].values[(size_t) tune];
        ses.recall (a);
        for (int i = 0; i < 40; ++i) e.render (b);
        const bool ok4 = std::abs (held - standsNow) < 1.0e-6f && ses.library[(size_t) ses.indexOf (withWave)].values[(size_t) cx::mot (2, W)] < 0.0f
                      && std::abs (e.target (cx::mot (2, W)) - wave ("RAMP UP")) < 1.0e-6f && e.target (cx::mot (2, A)) == 1.0f && ses.motionOn (4) == 4;
        say (ok4, "a snapshot taken while it runs holds where the knobs stand, and says nothing of MOTION; a recall leaves MOTION as it is");

        // a snapshot taken off the knob: the law is made again, and with one left it does nothing
        ses.knobs[4].snaps = { a, c };
        ses.turnSnapKnob (4, 0.0f);
        float span2 = 0.0f, o2 = 0.0f;
        for (int i = 0; i < perBar; ++i) { e.render (b); span2 = std::max (span2, e.shown (tune) - e.target (tune)); o2 = std::max (o2, std::abs (e.shown (other) - e.target (other))); }
        ses.knobs[4].snaps = { a };
        ses.turnSnapKnob (4, 0.0f);
        float span1 = 0.0f;
        for (int i = 0; i < perBar; ++i) { e.render (b); span1 = std::max (span1, std::abs (e.shown (tune) - e.target (tune))); }
        say (span2 > 0.55f && o2 < 1.0e-6f && span1 == 0.0f, "a snapshot taken off the knob changes what the wave does at once (two left: TUNE still goes, " + juce::String (span2, 2) + ", and the knob only the third moved does not); with one left it moves nothing");
        e.setRunning (false);
    }

    // 3. the kick as the wave
    {
        Engine e; e.prepare (SR, block);
        Session ses (e, false);
        juce::AudioBuffer<float> b (2, block);
        const int perBar = (int) std::lround (4.0 * 60.0 / e.tempo() * SR / block);
        ses.userSet (cx::kGlide, 0.0f);
        Pattern p = ses.pattern (0);
        for (int i = 0; i < (int) p.steps.size(); ++i) p.steps[(size_t) i].on = i % 4 == 0 && i < p.length;
        ses.setPattern (0, p);
        ses.knobs[0].links = { { tune, 0.5f } };
        ses.userSet (cx::mot (0, W), wave ("KICK")); ses.userSet (cx::mot (0, S), speed ("1/4")); ses.userSet (cx::mot (0, A), 1.0f);
        for (int i = 0; i < 20; ++i) e.render (b);
        const float stands = e.target (tune), before = e.shown (tune) - stands;
        e.setRunning (true);
        float top = 0.0f, low = 9.0f;
        int rises = 0;
        float was = 0.0f;
        for (int i = 0; i < perBar * 2; ++i)
        {
            e.render (b);
            const float v = e.shown (tune) - stands;
            if (i > perBar) { top = std::max (top, v); low = std::min (low, v); if (v > was + 0.1f) ++rises; }
            was = v;
        }
        e.setRunning (false);
        say (before == 0.0f && top > 0.3f && low < 0.5f * top && rises >= 3 && rises <= 5, "KICK as the wave: nothing before the kick plays; then each hit pushes the knob and it falls back before the next (up to +" + juce::String (top, 2) + ", down to +" + juce::String (low, 2)
                 + ", " + juce::String (rises) + " rises in a bar)");
    }

    // 4. with no MOTION on, the sound is to the sample what it was; with it on something nothing is tied to, too
    {
        auto run = [&] (int how)
        {
            Engine e; e.prepare (SR, block);
            Session ses (e, false);
            juce::AudioBuffer<float> b (2, block);
            ses.userSet (cx::kGlide, 0.0f);
            if (how == 1) { ses.userSet (cx::mot (0, W), wave ("SINE")); ses.userSet (cx::mot (1, W), wave ("DRIFT")); ses.userSet (cx::mot (1, K), knob (7)); }      // waves chosen, no amount
            if (how == 2 && ! ses.knobLive (15)) { ses.userSet (cx::mot (0, W), wave ("SINE")); ses.userSet (cx::mot (0, K), knob (15)); ses.userSet (cx::mot (0, A), 1.0f); ses.userSet (cx::kSnap + 15, 0.5f); }      // on a knob that holds nothing
            if (how == 3) { ses.knobs[15].links = { { cx::lane (7, 2), 0.5f } }; ses.userSet (cx::mot (0, W), wave ("SINE")); ses.userSet (cx::mot (0, K), knob (15)); ses.userSet (cx::mot (0, A), 0.8f); }       // on a knob tied to the lead
            e.setRunning (true);
            std::vector<float> out;
            const int blocks = (int) (8.0 * 60.0 / e.tempo() * SR / block);
            for (int i = 0; i < blocks; ++i) { e.render (b); out.insert (out.end(), b.getReadPointer (0), b.getReadPointer (0) + block); out.insert (out.end(), b.getReadPointer (1), b.getReadPointer (1) + block); }
            return out;
        };
        const auto plain = run (0), chosen = run (1), empty = run (2), on = run (3);
        double diff = 0.0, all = 0.0;
        for (size_t i = 0; i < plain.size(); ++i) { diff += (double) (on[i] - plain[i]) * (double) (on[i] - plain[i]); all += (double) plain[i] * (double) plain[i]; }
        const bool same = plain == chosen && plain == empty;
        say (same && all > 0.0 && diff > 1.0e-4 * all, "two bars of the rig are to the sample what they were with waves chosen and no AMOUNT, and with MOTION on a snapshot knob that holds nothing; on a knob tied to the lead they are not ("
                 + juce::String (10.0 * std::log10 (diff / (all + 1.0e-30) + 1.0e-30), 1) + " dB of difference)");
    }

    // 5. the file keeps it, by name, and it is at work again when the app opens
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_motion_test");
        tmp.deleteRecursively(); tmp.createDirectory();
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        bool inFile = false, backOn = false, swings = false;
        {
            Engine e; e.prepare (SR, block);
            Session ses (e, true);
            ses.knobs[6].links = { { tune, 0.3f } };
            ses.userSet (cx::mot (1, W), wave ("TRIANGLE")); ses.userSet (cx::mot (1, S), speed ("2 BAR")); ses.userSet (cx::mot (1, K), knob (6)); ses.userSet (cx::mot (1, A), 0.9f);
            ses.save();
            const var root = juce::JSON::parse (Session::stateFile().loadFileAsString());
            inFile = std::abs ((double) root["controls"]["mot.2_wave"] - (double) wave ("TRIANGLE")) < 1.0e-6 && std::abs ((double) root["controls"]["mot.2_amount"] - 0.9) < 1.0e-6
                  && std::abs ((double) root["controls"]["mot.2_knob"] - (double) knob (6)) < 1.0e-6 && root["controls"].hasProperty ("mot.2_speed");
        }
        {
            Engine e; e.prepare (SR, block);
            Session ses (e, true);
            juce::AudioBuffer<float> b (2, block);
            backOn = e.valueText (cx::mot (1, W)) == "TRIANGLE" && e.valueText (cx::mot (1, S)) == "2 BAR" && e.valueText (cx::mot (1, K)) == "7" && e.valueText (cx::mot (1, A)) == "+80" && ses.motionOn (6) == 2;
            float lo = 9.0f, hi = -9.0f;
            for (int i = 0; i < (int) (8.0 * 60.0 / e.tempo() * SR / block); ++i) { e.render (b); lo = std::min (lo, e.motionPush (tune)); hi = std::max (hi, e.motionPush (tune)); }
            swings = hi > 0.2f && lo == 0.0f;           // (the knob stands at nought: the wave's lower half has nowhere to go)
        }
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
        say (inFile && backOn && swings, "MOTION is in the session file by name (mot.2_wave ...), and when the app opens it is on the knob it was on and at work (TRIANGLE, 2 BAR, knob 7, +80)");
        if (! (inFile && backOn && swings)) std::printf ("        (file %d back %d swings %d)\n", inFile, backOn, swings);
    }
}

// The arrangement: snapshots in a line, played from the clock and printed to files.
static void arrangementTest (int& bad)
{
    using namespace sf0;
    using juce::var;
    const double SR = 48000.0;
    const int block = 128;
    auto say = [&bad] (bool ok, const juce::String& what) { std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++bad; };
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_arrange_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    Session::stateFileForTests = tmp.getChildFile ("session.json");
    const int cut = cx::lane (5, 6), hatLevel = cx::lane (1, 11);
    juce::int64 A = 0, B = 0, C = 0;
    juce::MemoryBlock firstSet;
    {
        Engine e; e.prepare (SR, block);
        Session ses (e, true);
        juce::AudioBuffer<float> b (2, block);
        const double beat = 60.0 / e.tempo(), spb = beat * SR;
        auto run = [&] (int blocks) { for (int i = 0; i < blocks; ++i) e.render (b); };
        ses.userSet (cx::kGlide, 0.0f);
        // three scenes: the bass's filter in three places, the key off, D and off; a kick on every beat in the first two and none in the third
        auto kick = [&] (bool on) { Pattern p = ses.pattern (0); for (int i = 0; i < (int) p.steps.size(); ++i) p.steps[(size_t) i].on = on && i % 4 == 0 && i < p.length; ses.setPattern (0, p); };
        kick (true); ses.userSet (cut, 0.20f); ses.userSet (hatLevel, 0.30f); ses.userSet (cx::kKey, 0.0f); run (30);
        A = ses.take();
        ses.userSet (cut, 0.80f); ses.userSet (hatLevel, 0.60f); ses.userSet (cx::kKey, 3.0f / 12.0f); run (30);
        B = ses.take();
        kick (false); ses.userSet (cut, 0.40f); ses.userSet (cx::kKey, 0.0f); run (30);
        C = ses.take();
        ses.finishTakes (true);
        ses.recall (A); run (30);
        const bool none = ses.arrBars() == 0 && ! ses.arrPlaying();
        ses.arrPlay();                                  // (nothing in it: nothing happens)
        const bool nothing = ! ses.arrPlaying() && ! e.isRunning();
        const bool added = ses.arrAdd (A) && ses.arrAdd (B) && ses.arrAdd (C) && ! ses.arrAdd (123456789);
        ses.arrSet (0, 2, 0); ses.arrSet (1, 2, 3); ses.arrSet (2, 1, 0);          // A two bars; B two, morphing in over one; C one, cut
        say (none && nothing && added && ses.arrangement.size() == 3 && ses.arrBars() == 5 && ses.arrBarsBefore (2) == 4 && ses.arrMorphBars (1) == 1.0 && Session::morphName (3) == "1 BAR"
                 && std::abs (ses.arrSeconds() - 20.0 * beat) < 1.0e-9,
             "an arrangement is parts in a line, each a snapshot, its bars and its way in: three of them make five bars (" + juce::String (ses.arrSeconds(), 2) + " s), and a snapshot that is not there cannot be a part");

        // ---- played: each part comes in on its bar line
        ses.recall (C); run (30);                       // (somewhere else first: PLAY brings the first part)
        ses.arrPlay();
        const bool atOnce = ses.baseId == A && std::abs (e.target (cut) - 0.20f) < 1.0e-6f && ses.arrPlaying() && ses.arrPart() == 0 && e.isRunning();
        float before8 = -1.0f, at9 = -1.0f, at10 = -1.0f, at12 = -1.0f, before16 = -1.0f, after16 = -1.0f, keyBefore8 = -1.0f, keyAfter8 = -1.0f;
        double recalledB = -1.0, recalledC = -1.0, stoppedAt = -1.0;
        bool patB = false;
        for (int i = 0; i < (int) (22.0 * spb / block) && (ses.arrPlaying() || i < 10); ++i)
        {
            const double p0 = e.songPos();
            e.render (b);
            ses.arrTick();
            const double p1 = e.songPos();
            if (recalledB < 0.0 && ses.baseId == B) recalledB = p1;
            if (recalledC < 0.0 && ses.baseId == C) recalledC = p1;
            auto crossed = [&] (double at) { return p0 < at && p1 >= at; };
            if (crossed (7.97)) { before8 = e.shown (cut); keyBefore8 = e.shown (cx::kKey); }
            if (crossed (8.1)) keyAfter8 = e.shown (cx::kKey);
            if (crossed (9.0)) at9 = e.shown (cut);
            if (crossed (10.0)) at10 = e.shown (cut);
            if (crossed (12.05)) { at12 = e.shown (cut); patB = ses.pattern (0).steps[0].on; }
            if (crossed (15.97)) before16 = e.shown (cut);
            if (crossed (16.2)) after16 = e.shown (cut);
            if (stoppedAt < 0.0 && ! e.isRunning()) stoppedAt = p1;
        }
        const bool onTheBar = recalledB > 7.4 && recalledB < 8.0 && std::abs (before8 - 0.20f) < 0.004f && keyBefore8 == 0.0f && std::abs (keyAfter8 - 0.25f) < 1.0e-6f;
        const bool travels = std::abs (at9 - 0.35f) < 0.012f && std::abs (at10 - 0.50f) < 0.012f && std::abs (at12 - 0.80f) < 1.0e-4f && patB;
        const bool cuts = recalledC > 15.4 && recalledC < 16.0 && std::abs (before16 - 0.80f) < 1.0e-4f && std::abs (after16 - 0.40f) < 1.0e-4f && ! ses.pattern (0).steps[0].on;
        const bool ends = ! ses.arrPlaying() && ! e.isRunning() && stoppedAt > 19.9 && stoppedAt < 20.0 + 1.0e-6 && ses.arrPart() == -1;
        say (atOnce && onTheBar && travels && cuts && ends,
             "PLAY from stopped brings its first part at once and starts the clock. The second is recalled inside the beat before its bar (at beat " + juce::String (recalledB, 2) + ") and nothing moves until the bar line: the filter is where it was at 7.97 ("
                 + juce::String (before8, 3) + "), the key goes over on the line, and the filter travels its bar (" + juce::String (at9, 3) + " a beat in, " + juce::String (at10, 3) + " at half, " + juce::String (at12, 3)
                 + " when it is done). A part that cuts is there on its line (" + juce::String (before16, 3) + " before, " + juce::String (after16, 3) + " after). At its end the clock stops, short of the next bar (beat " + juce::String (stoppedAt, 3) + ")");
        if (! (atOnce && onTheBar && travels && cuts && ends)) std::printf ("        (at once %d, on the bar %d [B at %.3f, key %.3f then %.3f], travels %d [pattern %d], cuts %d [C at %.3f], ends %d)\n", atOnce, onTheBar, recalledB, keyBefore8, keyAfter8, travels, patB, cuts, recalledC, ends);

        // ---- with the clock running, a part comes in on the next bar line; RUN stopped by hand puts it down
        ses.recall (A); run (10);
        e.setRunning (true); e.render (b);
        while (e.songPos() < 5.3) e.render (b);
        ses.arrPlay (1);
        double bAt = -1.0; float cutAt8 = -1.0f, cutBefore = -1.0f;
        while (e.songPos() < 12.5)
        {
            const double p0 = e.songPos();
            e.render (b); ses.arrTick();
            if (bAt < 0.0 && ses.baseId == B) bAt = e.songPos();
            if (p0 < 7.97 && e.songPos() >= 7.97) cutBefore = e.shown (cut);
            if (p0 < 10.0 && e.songPos() >= 10.0) cutAt8 = e.shown (cut);
        }
        const bool joined = bAt > 7.4 && bAt < 8.0 && std::abs (cutBefore - 0.20f) < 0.004f && std::abs (cutAt8 - 0.50f) < 0.012f && ses.arrPlaying() && ses.arrPart() == 1 && std::abs (ses.arrPos() - (e.songPos() - 0.0)) < 1.0e-9;
        e.setRunning (false); e.render (b); ses.arrTick();
        const bool putDown = ! ses.arrPlaying();
        say (joined && putDown, "started while the clock runs (at beat 5.3, from its second part), that part comes in on the next bar line (recalled at " + juce::String (bAt, 2) + ", half way at beat 10: " + juce::String (cutAt8, 3) + "); RUN stopped by hand puts the arrangement down");

        // ---- a recall with a time of its own: a switch that cannot travel goes over on the beat, not before
        ses.recall (A); run (10);
        e.setRunning (true); e.render (b);
        while (e.songPos() < 2.4) e.render (b);
        ses.recallOver (B, 4.0, true);
        e.render (b);
        const float keyNow = e.shown (cx::kKey);
        const bool said = std::abs (e.target (cx::kKey) - 0.25f) < 1.0e-6f;
        while (e.songPos() < 3.05) e.render (b);
        const float keyThen = e.shown (cx::kKey);
        ses.userSet (cx::kGlide, 0.0f);
        ses.recall (A); e.render (b);
        const float keyCut = e.shown (cx::kKey);
        e.setRunning (false); run (4);
        say (said && keyNow == 0.0f && std::abs (keyThen - 0.25f) < 1.0e-6f && keyCut == 0.0f, "a recall that travels takes the key over on the beat its knobs set off on (asked for at beat 2.4: still the old key then, the new one after beat 3); with GLIDE off it is there at once");

        // ---- printed
        ses.recall (B); run (20);
        ses.userSet (cx::kSnap + 15, 0.0f);
        const float cutWas = e.target (cut);
        std::vector<float> rigWas; for (int i = 0; i < cx::kCount; ++i) rigWas.push_back (e.target (i));
        const auto blocksWas = e.starts();
        int paused = 0, resumed = 0;
        ses.audioPause = [&paused] { ++paused; };
        ses.audioResume = [&resumed] { ++resumed; };
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const juce::String err = ses.printBegin (tmp.getChildFile ("prints"));
        const bool began = err.isEmpty() && ses.printing() && ! e.isOffline() && paused == 1;
        int steps = 0; float most = 0.0f; bool rose = true;
        while (ses.printStep (20.0)) { ++steps; rose = rose && ses.printDone() >= most; most = std::max (most, ses.printDone()); }
        const double took = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
        const auto dir = ses.printFolder;
        const bool done = ! ses.printing() && ! e.isOffline() && resumed == 1 && ! e.isRunning() && ! ses.arrPlaying() && ses.printNote.startsWith ("PRINTED");
        bool untouched = e.starts() == blocksWas;
        for (int i = 0; i < cx::kCount; ++i) untouched = untouched && e.target (i) == rigWas[(size_t) i];
        const bool putBack = untouched && ses.baseId == B && std::abs (e.target (cut) - cutWas) < 1.0e-6f && std::abs (e.target (cx::kKey) - 0.25f) < 1.0e-6f && ses.pattern (0).steps[0].on;
        // what is in the files
        juce::WavAudioFormat wav;
        auto read = [&] (const juce::String& name, juce::AudioBuffer<float>& into, int& bits)
        {
            std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (dir.getChildFile (name).createInputStream().release(), true));
            if (r == nullptr) return false;
            into.setSize (2, (int) r->lengthInSamples);
            r->read (&into, 0, (int) r->lengthInSamples, 0, true, true);
            bits = (int) r->bitsPerSample;
            return std::abs (r->sampleRate - SR) < 0.5;
        };
        juce::AudioBuffer<float> set, kickStem;
        int setBits = 0, kickBits = 0;
        const bool files = read ("00 SET.wav", set, setBits) && read ("01 KICK.wav", kickStem, kickBits) && setBits == 24 && kickBits == 32 && dir.getChildFile ("PRINT.txt").existsAsFile();
        const auto total = (juce::int64) std::llround (20.0 * spb);
        auto peakIn = [] (const juce::AudioBuffer<float>& a, juce::int64 from, juce::int64 to) { from = juce::jlimit ((juce::int64) 0, (juce::int64) a.getNumSamples(), from); to = juce::jlimit (from, (juce::int64) a.getNumSamples(), to); return to > from ? a.getMagnitude ((int) from, (int) (to - from)) : 0.0f; };
        const float kickTop = files ? peakIn (kickStem, 0, kickStem.getNumSamples()) : 0.0f;
        // bar 1 begins on the first sample: the kick's first hit is there, in its own file and in the set
        int kickAt = -1, setAt = -1;
        if (files) for (int i = 0; i < kickStem.getNumSamples() && kickAt < 0; ++i) if (std::abs (kickStem.getSample (0, i)) > 0.02f * kickTop) kickAt = i;
        if (files) for (int i = 0; i < set.getNumSamples() && setAt < 0; ++i) if (std::abs (set.getSample (0, i)) > 0.02f * peakIn (set, 0, (juce::int64) spb)) setAt = i;
        // a hit on every beat of the first four bars, and none in the fifth
        int hits = 0, late = 0;
        for (int k = 0; files && k < 20; ++k)
        {
            const auto at = (juce::int64) std::llround ((double) k * spb);
            const bool hit = peakIn (kickStem, at, at + 480) > 3.0f * peakIn (kickStem, at - 600, at - 60) + 0.01f * kickTop;
            if (hit && k < 16) ++hits;
            if (hit && k >= 16) ++late;
        }
        const double len = files ? (double) set.getNumSamples() / SR : 0.0;
        const bool inStep = files && set.getNumSamples() == kickStem.getNumSamples();
        const bool tail = files && set.getNumSamples() > total && len < 20.0 * beat + 60.5 && peakIn (set, set.getNumSamples() - 4800, set.getNumSamples()) < 1.0e-4f;
        const juce::String notes = dir.getChildFile ("PRINT.txt").loadFileAsString();
        const bool noted = notes.contains ("5 bars at 132.0") && notes.contains ("BAR    3") && notes.contains ("MORPH 1 BAR") && notes.contains ("LUFS");
        if (std::getenv ("SF000_SHOW_PRINT") != nullptr) std::printf ("%s\n", notes.toRawUTF8());
        say (began && done && rose && steps > 2 && putBack && files && kickAt >= 0 && kickAt < 96 && setAt >= 0 && setAt < 96 && hits == 16 && late == 0 && inStep && tail && noted,
             "PRINT renders it to a folder, the audio stopped for it and started again, in " + juce::String (took, 1) + " s for " + juce::String (len, 1) + " s of sound: the set (24-bit) and the parts it was made of (32-bit float, as long as it), "
                 "bar 1 on the first sample (the kick's first hit " + juce::String (kickAt) + " samples in, in its own file, and " + juce::String (setAt) + " in the set), " + juce::String (hits) + " kicks on the beats of the four bars that have them and "
                 + juce::String (late) + " in the fifth, what rings on after the last bar kept (" + juce::String (len - 20.0 * beat, 1) + " s), and a page saying what is in it. It is rendered on a rig of its own: the one being played is not touched");
        if (! (began && done && putBack && files)) std::printf ("        (began %d [%s], done %d [%s], rose %d, steps %d, put back %d, files %d [%d %d bit])\n", began, err.toRawUTF8(), done, ses.printNote.toRawUTF8(), rose, steps, putBack, files, setBits, kickBits);
        if (files) dir.getChildFile ("00 SET.wav").loadFileAsData (firstSet);
        // twice is the same, to the byte
        const juce::String err2 = ses.printBegin (tmp.getChildFile ("prints"));
        while (ses.printStep (50.0)) {}
        juce::MemoryBlock again;
        ses.printFolder.getChildFile ("00 SET.wav").loadFileAsData (again);
        juce::MemoryBlock kick1, kick2;
        dir.getChildFile ("01 KICK.wav").loadFileAsData (kick1); ses.printFolder.getChildFile ("01 KICK.wav").loadFileAsData (kick2);
        say (err2.isEmpty() && ses.printFolder != dir && firstSet.getSize() > 1000 && firstSet == again && kick1 == kick2, "printed again, the files are the same to the byte");
        if (! (firstSet == again))
            for (int t = 0; t < Engine::kRecTracks; ++t)
            {
                juce::AudioBuffer<float> x, y; int bx = 0;
                const juce::String name = Recorder::trackName (t) + ".wav";
                if (! dir.getChildFile (name).existsAsFile() || ! ses.printFolder.getChildFile (name).existsAsFile()) { std::printf ("        [%s: in one only]\n", name.toRawUTF8()); continue; }
                read (name, x, bx);
                { std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (ses.printFolder.getChildFile (name).createInputStream().release(), true)); y.setSize (2, (int) r->lengthInSamples); r->read (&y, 0, (int) r->lengthInSamples, 0, true, true); }
                int first = -1; double d = 0.0, a = 0.0;
                for (int i = 0; i < std::min (x.getNumSamples(), y.getNumSamples()); ++i) { const double q = x.getSample (0, i) - y.getSample (0, i); if (q != 0.0 && first < 0) first = i; d += q * q; a += (double) x.getSample (0, i) * x.getSample (0, i); }
                std::printf ("        [%s: %d and %d samples, first differs at %d (beat %.2f), %.1f dB]\n", name.toRawUTF8(), x.getNumSamples(), y.getNumSamples(), first, first / spb, 10.0 * std::log10 (d / (a + 1e-30) + 1e-30));
            }
        // stopped part way: nothing is left, and the rig is as it was
        const juce::String err3 = ses.printBegin (tmp.getChildFile ("prints"));
        for (int i = 0; i < 3; ++i) ses.printStep (10.0);
        const auto dir3 = ses.printFolder;
        ses.printCancel();
        say (err3.isEmpty() && ! dir3.exists() && ! ses.printing() && ! e.isOffline() && ses.baseId == B && std::abs (e.target (cut) - cutWas) < 1.0e-6f && ses.printNote == "PRINT STOPPED" && paused == 3 && resumed == 3,
             "a print stopped part way leaves no files, and the rig as it was");

        // ---- parts moved, a snapshot deleted, and the file
        ses.arrMove (2, -1);
        const bool moved = ses.arrangement[1].snap == C && ses.arrangement[2].snap == B && ses.arrBarsBefore (2) == 3;
        ses.arrMove (1, 1);
        ses.arrangement[1].more.set ("colour", "red");
        ses.save();
        const var root = juce::JSON::parse (Session::stateFile().loadFileAsString());
        const bool inFile = root["arrangement"]["parts"].size() == 3 && (juce::int64) root["arrangement"]["parts"][1]["snap"] == B && (int) root["arrangement"]["parts"][1]["bars"] == 2
                         && std::abs ((double) root["arrangement"]["parts"][1]["morph"] - 1.0) < 1.0e-9 && root["arrangement"]["parts"][1]["colour"].toString() == "red";
        say (moved && inFile, "a part is moved earlier or later; the session file holds the arrangement (each part's snapshot, its bars, its way in as bars), and what it does not know of a part is carried");
    }
    {
        Engine e; e.prepare (SR, block);
        Session ses (e, true);
        const bool back = ses.arrangement.size() == 3 && ses.arrangement[0].snap == A && ses.arrangement[1].snap == B && ses.arrangement[1].bars == 2 && ses.arrangement[1].morph == 3
                       && ses.arrangement[1].more["colour"].toString() == "red" && ses.arrangement[2].snap == C && ses.arrangement[2].bars == 1 && ses.arrangement[2].morph == 0;
        ses.remove (B);
        say (back && ses.arrangement.size() == 2 && ses.arrangement[1].snap == C && ses.arrBars() == 3, "it is there when the app opens; a snapshot deleted takes its parts with it");
    }
    Session::stateFileForTests = juce::File();
    tmp.deleteRecursively();
}

// The convolution's BANDS: three places across the spectrum. Its response to one click is taken
// (with the module rendering as it does offline, so each change is built before the click), and
// looked at band by band: how much is there, and how much of that is late.
static void convolutionBandsTest (int& bad)
{
    using namespace sf0;
    const double SR = 48000.0;
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_bands_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    Session::homeForTests = tmp.getChildFile ("home");
    Session::homeForTests.createDirectory();
    // two places: noise dying away quickly (a third of a second), and slowly (two seconds)
    auto writeIr = [&] (const char* name, double seconds, int seed)
    {
        const auto f = tmp.getChildFile (name);
        const int n = (int) (SR * std::min (4.0, seconds * 1.6));
        juce::AudioBuffer<float> x (2, n);
        juce::Random rng (seed);
        const double rate = 6.9078 / seconds;
        for (int i = 0; i < n; ++i) for (int ch = 0; ch < 2; ++ch) x.setSample (ch, i, (i == 0 ? 0.9f : 0.5f * (rng.nextFloat() * 2.0f - 1.0f)) * (float) std::exp (-rate * i / SR));
        juce::WavAudioFormat wav;
        if (auto out = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
            if (auto* w = wav.createWriterFor (out.get(), SR, 2, 24, {}, 0)) { out.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (x, 0, n); }
        return f;
    };
    const auto shortIr = writeIr ("short.wav", 0.35, 5), longIr = writeIr ("long.wav", 2.0, 9);
    {
        Engine e; e.prepare (SR, 512);
        const Engine::Target t { 1, 7, 0 };
        e.setInsertType (7, 0, 2);                      // CONVOLUTION, on the lead
        auto* p = e.processorFor (t);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer noMidi;
        auto silence = [&] (int blocks) { for (int i = 0; i < blocks; ++i) { b.clear(); p->processBlock (b, noMidi); } };
        auto settle = [&] { for (int tries = 0; tries < 400; ++tries) { silence (8); juce::Thread::sleep (10); if (! p->mod().statusLine().startsWith ("IMPORTING") && tries > 8) break; } };
        auto set = [&] (const char* id, float plain) { if (auto* prm = p->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); };
        e.setSetup (t, "import", 1.0f / 3.0f);          // IR FILE: every arrival kept
        e.loadFiles (t, juce::StringArray (shortIr.getFullPathName())); settle();     // place 1: the short one
        set ("place", 1.0f);
        e.loadFiles (t, juce::StringArray (longIr.getFullPathName())); settle();      // place 2: the long one
        set ("place", 0.0f);
        set ("mix", 1.0f); set ("dry", 0.0f); set ("duck", 0.0f); set ("lo_cut", 20.0f); set ("dark", 0.0f); set ("size", 1.0f); set ("decay", 1.0f);
        p->setNonRealtime (true);                       // (as in a render: a change is built here, before the next sound)
        // The response to one click: three seconds of it. Into three bands (under 250 Hz, to 2.5 kHz,
        // over), each as its level in all and its level after the first half second.
        struct Seen { double all[3] = {}, late[3] = {}; double total = 0.0; };
        auto click = [&]
        {
            silence (200);                              // (whatever was asked is built and in, and the last tail is gone)
            std::vector<float> h;
            for (int blk = 0; blk < (int) (3.0 * SR / 512); ++blk)
            {
                b.clear();
                if (blk == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
                p->processBlock (b, noMidi);
                for (int i = 0; i < 512; ++i) h.push_back (b.getSample (0, i));
            }
            // (two fourth-order band edges, as crossovers are: the bands' powers then add up to the whole)
            auto lr = [&] (std::vector<double> x, double hz, bool high)
            {
                const double w = juce::MathConstants<double>::twoPi * hz / SR, cs = std::cos (w), al = std::sin (w) / std::sqrt (2.0), a0 = 1.0 + al;
                const double b1 = (high ? -(1.0 + cs) : 1.0 - cs) / a0, b0 = (high ? 1.0 + cs : 1.0 - cs) * 0.5 / a0, a1 = -2.0 * cs / a0, a2 = (1.0 - al) / a0;
                for (int twice = 0; twice < 2; ++twice) { double z1 = 0.0, z2 = 0.0; for (auto& v : x) { const double out = b0 * v + z1; z1 = b1 * v - a1 * out + z2; z2 = b0 * v - a2 * out; v = out; } }
                return x;
            };
            const std::vector<double> x (h.begin(), h.end());
            const auto lowMid = lr (x, 2500.0, false);
            const std::vector<double> band[3] = { lr (lowMid, 250.0, false), lr (lowMid, 250.0, true), lr (x, 2500.0, true) };
            Seen sn;
            for (int k = 0; k < 3; ++k)
            {
                double a = 0.0, l = 0.0;
                for (size_t i = 0; i < band[k].size(); ++i) { const double q = band[k][i] * band[k][i]; a += q; if (i > (size_t) (0.5 * SR)) l += q; }
                sn.all[k] = 10.0 * std::log10 (a + 1.0e-20); sn.late[k] = 10.0 * std::log10 (l + 1.0e-20);
                sn.total += a;
            }
            sn.total = 10.0 * std::log10 (sn.total + 1.0e-20);
            return sn;
        };
        auto bandsOff = [&] { set ("bands", 0.0f); };
        auto bandsOn = [&] (int lowPlace, int midPlace, int highPlace)
        {
            set ("bands", 1.0f); set ("bx_low", 250.0f); set ("bx_high", 2500.0f);
            const int places[3] = { lowPlace, midPlace, highPlace };
            for (int k = 0; k < 3; ++k)
            {
                const juce::String pre = "b" + juce::String (k + 1) + "_";
                set ((pre + "place").toRawUTF8(), (float) places[k]); set ((pre + "level").toRawUTF8(), 0.0f); set ((pre + "decay").toRawUTF8(), 1.0f); set ((pre + "size").toRawUTF8(), 1.0f);
            }
        };
        bandsOff();
        const Seen shortOne = click();
        set ("place", 1.0f);
        const Seen longOne = click();
        set ("place", 0.0f);
        const bool twoPlaces = longOne.late[1] > shortOne.late[1] + 25.0 && shortOne.total > -60.0;

        // the three bands of one place are that place
        bandsOn (0, 0, 0);
        const Seen same = click();
        double worst = 0.0;
        for (int k = 0; k < 3; ++k) worst = std::max (worst, std::abs (same.all[k] - shortOne.all[k]));
        {
            const bool ok = twoPlaces && worst < 0.5 && std::abs (same.total - shortOne.total) < 0.3;
            std::printf ("  %s: CONVOLUTION, BANDS: with all three bands on the place PLACE names it is that place again (each band within %.2f dB, the whole %+.2f dB)\n", ok ? "ok  " : "FAIL", worst, same.total - shortOne.total);
            if (! ok) ++bad;
        }
        // each band from a place of its own: the long place in the middle alone
        {
            bandsOn (1, 2, 1);
            const Seen mixed = click();
            // (the bands meet at 18 dB an octave, so some of the middle's long tail is still to be found just under 250 Hz and over 2.5 kHz)
            const bool ok = std::abs (mixed.late[1] - longOne.late[1]) < 1.5 && mixed.late[0] < longOne.late[0] - 8.0 && mixed.late[2] < longOne.late[2] - 15.0
                         && std::abs (mixed.all[0] - shortOne.all[0]) < 1.0 && std::abs (mixed.all[1] - longOne.all[1]) < 1.0 && std::abs (mixed.all[2] - shortOne.all[2]) < 1.0;
            std::printf ("  %s: the long place in the middle band and the short one below and above: half a second on, the middle is the long place's (%+.1f dB against it) and the low and the top have died away as the short one's do (%.0f and %.0f dB under the long place's)\n",
                         ok ? "ok  " : "FAIL", mixed.late[1] - longOne.late[1], longOne.late[0] - mixed.late[0], longOne.late[2] - mixed.late[2]);
            if (! ok) ++bad;
        }
        // and the same when it is not rendering: the bands go to the worker that builds the response, and come back as asked
        {
            bandsOff(); silence (200);
            p->setNonRealtime (false);
            bandsOn (1, 2, 1);
            for (int tries = 0; tries < 60; ++tries) { silence (8); juce::Thread::sleep (10); }
            const Seen live = click();
            p->setNonRealtime (true);
            bandsOn (1, 2, 1);
            const Seen rendered = click();
            double off = 0.0;
            for (int k = 0; k < 3; ++k) off = std::max ({ off, std::abs (live.all[k] - rendered.all[k]), std::abs (live.late[k] - rendered.late[k]) });
            const bool ok = off < 0.05 && std::abs (live.late[1] - longOne.late[1]) < 1.5;
            std::printf ("  %s: played live (the response built on its worker's thread and crossed to) it is the response a render builds: %.3f dB apart at most\n", ok ? "ok  " : "FAIL", off);
            if (! ok) ++bad;
        }
        // a band's LEVEL, DECAY and SIZE are that band's alone; the crossovers move where the bands meet
        {
            bandsOn (2, 2, 2);
            const Seen base = click();
            set ("b1_level", -12.0f);
            const Seen lowDown = click();
            set ("b1_level", 0.0f); set ("b3_decay", 0.2f);
            const Seen topShort = click();
            set ("b3_decay", 1.0f); set ("b2_size", 2.0f);
            const Seen midBig = click();
            set ("b2_size", 1.0f); set ("b1_level", -24.0f); set ("bx_low", 1000.0f);
            const Seen wideLow = click();                // (the low band, turned right down, now reaches up to 1 kHz: the middle band of the test loses what is under that)
            set ("bx_low", 250.0f);
            const Seen narrowLow = click();
            const bool level = lowDown.all[0] < base.all[0] - 6.0 && lowDown.all[0] > base.all[0] - 12.5 && std::abs (lowDown.all[1] - base.all[1]) < 0.5 && std::abs (lowDown.all[2] - base.all[2]) < 0.3;
            const bool decay = topShort.late[2] < base.late[2] - 15.0 && std::abs (topShort.late[1] - base.late[1]) < 2.0;     // (some of the top's tail was under 2.5 kHz: its skirt)
            const bool size = midBig.late[1] > base.late[1] + 2.0 && std::abs (midBig.late[2] - base.late[2]) < 0.5;
            const bool cross = wideLow.all[1] < narrowLow.all[1] - 1.0;
            const bool ok = level && decay && size && cross;
            std::printf ("  %s: the low band's LEVEL at -12 dB takes %.1f dB from what is under 250 Hz (the middle band's own skirt is the rest of what is there) and nothing from the others (%+.1f, %+.1f); the top band's DECAY at a fifth lets the top die away (%.0f dB less of it half a second on) with the middle as it was; "
                         "the middle band's SIZE at 200 %% lengthens the middle (%+.1f dB late); LOW/MID moved from 250 Hz to 1 kHz gives the low band that much more of the spectrum (the middle is %.1f dB less)\n",
                         ok ? "ok  " : "FAIL", base.all[0] - lowDown.all[0], lowDown.all[1] - base.all[1], lowDown.all[2] - base.all[2], base.late[2] - topShort.late[2], midBig.late[1] - base.late[1], narrowLow.all[1] - wideLow.all[1]);
            if (! ok) ++bad;
        }
        // BANDS off: the band knobs are nothing, and the response is the one it was, sample for sample
        {
            bandsOff(); set ("place", 0.0f);
            set ("b1_level", -24.0f); set ("b2_place", 2.0f); set ("b3_size", 2.0f); set ("bx_low", 700.0f);
            const Seen again = click();
            bool exact = true; double off = 0.0;
            for (int k = 0; k < 3; ++k) { off = std::max (off, std::abs (again.all[k] - shortOne.all[k])); exact = exact && std::abs (again.all[k] - shortOne.all[k]) < 0.01; }
            // CONFIG lists the bands' knobs only while BANDS is on, and a snapshot's setup holds them
            p->setNonRealtime (false);
            int offCount = 0, onCount = 0, groups = 0;
            for (auto& sp : e.setupParams (t)) if (sp.group.contains ("BAND")) ++offCount;
            set ("bands", 1.0f);
            juce::StringArray seenGroups;
            bool said = true;                           // (each has a line for INFO)
            for (auto& sp : e.setupParams (t)) if (sp.group.contains ("BAND")) { ++onCount; seenGroups.addIfNotAlreadyThere (sp.group); said = said && help::param ("TERROIR", sp.id.toRawUTF8()) != nullptr; }
            groups = seenGroups.size();
            const int listed = (int) e.setupParams (t).size();
            bool kept = false;
            for (auto& kv : e.getSetup (t)) kept = kept || kv.first == "b2_place";
            const bool ok = exact && offCount == 1 && onCount == 15 && groups == 4 && kept && said;
            std::printf ("  %s: with BANDS off the bands' knobs do nothing (the response is the one it was: %.4f dB apart at most); CONFIG lists BANDS alone while it is off and its fourteen knobs in four boxes while it is on (%d controls in all), and they are in what a snapshot keeps of the effect\n",
                         ok ? "ok  " : "FAIL", off, listed);
            if (! ok) { std::printf ("        (exact %d off %d on %d groups %d kept %d)\n", exact, offCount, onCount, groups, kept); ++bad; }
        }
    }
    Session::homeForTests = juce::File();
    tmp.deleteRecursively();
}

static int selfTest (const juce::String& wavPath, double bars = 16.0)
{
    using namespace sf0;
    int bad = 0;
    // (SF000_TEST_NEW set: the newest checks alone, POCKETS', for when they are what is being worked on)
    if (std::getenv ("SF000_TEST_NEW") != nullptr) { std::printf ("SF-000 offline checks: the newest only\n"); { const char w = std::getenv ("SF000_TEST_NEW")[0]; if (w == 'm') knobMotionTest (bad); else if (w == 'a') arrangementTest (bad); else { if (w != 'b') pocketsTest (bad); convolutionBandsTest (bad); } } std::printf ("%s\n", bad == 0 ? "(the newest checks only: passed)" : "(the newest checks only: FAILED)"); return bad; }
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

    // The DRUM BUS, in the master. Heard on the kick alone: what the bus hands on is the kick's own
    // stem and what the bus is recorded as doing to it, added together.
    {
        struct Tap : Engine::RecSink
        {
            std::vector<float> lane, bus, set; int want = 1;
            void begin (int) noexcept override {}
            void track (int idx, const float* l, const float*, int n) noexcept override
            {
                auto& into = idx == want ? lane : idx == 1 + kLanes + 2 ? bus : set;
                if ((idx == want || idx == 1 + kLanes + 2 || idx == 0) && into.size() + (size_t) n <= into.capacity()) into.insert (into.end(), l, l + n);
            }
            void end (int) noexcept override {}
        };
        enum { kDrive, kCrunch, kPunch, kBoom };
        using Knobs = std::vector<std::pair<int, float>>;
        using Setup = std::vector<std::pair<const char*, float>>;
        const int block = 256;
        int lane = 0;                           // the lane that plays alone
        double beat = 0.0;
        std::vector<float> did, whole;          // (of the last run: what the bus did, and the set)
        auto run = [&] (const Knobs& knobs, const Setup& setup = {}, float key = 0.0f, float level = -1.0f, bool alone = true)
        {
            Engine e; e.prepare (48000.0, block);
            if (alone) for (int l = 0; l < kLanes; ++l) if (l != lane) e.set (cx::lane (l, cx::kMute), 1.0f, 0.0);       // one lane alone (the kick, unless said)
            for (auto& k : knobs) e.set (cx::kBus + k.first, k.second, 0.0);
            for (auto& sp : setup) e.setSetup ({ 3, 0, 0 }, sp.first, sp.second);
            e.set (cx::kKey, key, 0.0);
            if (level >= 0.0f) e.set (cx::lane (lane, 11), level, 0.0);
            beat = 60.0 / e.tempo();
            Tap tap; tap.want = 1 + lane;
            for (auto* v : { &tap.lane, &tap.bus, &tap.set }) v->reserve (48000 * 6);
            juce::AudioBuffer<float> b (2, block);
            e.setRunning (true);
            for (int i = 0; i < (int) (4.0 * beat * 48000.0 / block); ++i) e.render (b);       // a bar to settle in
            e.setRecSink (&tap);
            for (int i = 0; i < (int) (8.0 * beat * 48000.0 / block); ++i) e.render (b);       // two bars heard
            e.setRecSink (nullptr);
            did = tap.bus; whole = tap.set;
            std::vector<float> out = tap.lane;
            for (size_t i = 0; i < out.size() && i < tap.bus.size(); ++i) out[i] += tap.bus[i];
            return out;
        };
        auto db = [] (double p) { return 10.0 * std::log10 (p + 1.0e-20); };
        auto power = [] (const std::vector<float>& x) { double s = 0.0; for (float v : x) s += (double) v * v; return s / (double) std::max<size_t> (1, x.size()); };
        auto peak = [] (const std::vector<float>& x) { float p = 0.0f; for (float v : x) p = std::max (p, std::fabs (v)); return (double) p; };
        auto differ = [&] (const std::vector<float>& a, const std::vector<float>& b)
        {
            double d = 0.0, sum = 0.0;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i) { d += (double) (a[i] - b[i]) * (a[i] - b[i]); sum += (double) a[i] * a[i]; }
            return 10.0 * std::log10 ((d + 1.0e-20) / (sum + 1.0e-20));
        };
        // how much of one pitch there is (Goertzel), over the whole of it or over a stretch after each beat
        auto tone = [&] (const std::vector<float>& x, double hz, double fromMs = 0.0, double toMs = 1.0e9)
        {
            const double w = juce::MathConstants<double>::twoPi * hz / 48000.0, per = beat * 48000.0;
            double re = 0.0, im = 0.0; int count = 0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double ms = std::fmod ((double) i, per) / 48.0;
                if (ms < fromMs || ms >= toMs) continue;
                re += x[i] * std::cos (w * (double) i); im += x[i] * std::sin (w * (double) i); ++count;
            }
            return (re * re + im * im) / std::max (1.0, (double) count * (double) count);
        };
        // the sound above 150 Hz over a stretch after each beat (two one-pole high-passes)
        auto upper = [&] (const std::vector<float>& x, double fromMs, double toMs)
        {
            const double a = std::exp (-juce::MathConstants<double>::twoPi * 150.0 / 48000.0), per = beat * 48000.0;
            double z1 = 0.0, z2 = 0.0, sum = 0.0; int count = 0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                z1 = x[i] + a * (z1 - x[i]); const double h1 = x[i] - z1;
                z2 = h1 + a * (z2 - h1); const double h = h1 - z2;
                const double ms = std::fmod ((double) i, per) / 48.0;
                if (ms >= fromMs && ms < toMs) { sum += h * h; ++count; }
            }
            return sum / std::max (1, count);
        };
        const Setup offBus = { { "in_1", 0.0f }, { "in_2", 0.0f }, { "in_3", 0.0f }, { "in_4", 0.0f }, { "in_5", 0.0f } };

        // At rest it is not there: the whole set, every lane playing, is what it is with no lane on the bus.
        {
            run ({}, {}, 0.0f, -1.0f, false);
            const auto with = whole; const double didAtRest = peak (did);
            run ({}, offBus, 0.0f, -1.0f, false);
            const double d = differ (whole, with);
            const bool ok = with.size() > 48000 && power (with) > 1.0e-6 && didAtRest == 0.0 && d < -100.0;     // (the lanes are added up in another order: the last bit of a float)
            std::printf ("  %s: DRUM BUS: with its knobs at nought it does nothing (it is recorded as doing %.1e), and the set is the set with no lane on the bus (%.0f dB of difference)\n",
                         ok ? "ok  " : "FAIL", didAtRest, d);
            if (! ok) ++bad;
        }
        const auto plain = run ({});
        const bool there = plain.size() > 48000 && power (plain) > 1.0e-6;

        // every knob is heard: the four on the master's face, the four in its CONFIG
        {
            juce::String heard; bool all = there;
            struct K { const char* name; int knob; const char* id; Knobs with; };
            const K ks[8] = { { "DRIVE", kDrive, nullptr, {} }, { "CRUNCH", kCrunch, nullptr, {} }, { "PUNCH", kPunch, nullptr, {} }, { "BOOM", kBoom, nullptr, {} },
                              { "DAMP", -1, "damp", { { kCrunch, 0.6f } } }, { "BOOM TUNE", -1, "boom_tune", { { kBoom, 0.7f } } },
                              { "BOOM DECAY", -1, "boom_decay", { { kBoom, 0.7f } } }, { "MIX", -1, "mix", { { kDrive, 1.0f } } } };
            for (int c = 0; c < 8; ++c)
            {
                Knobs lo = ks[c].with, hi = ks[c].with;
                Setup slo, shi;
                if (ks[c].knob >= 0) { lo.push_back ({ ks[c].knob, 0.0f }); hi.push_back ({ ks[c].knob, 1.0f }); }
                else { slo.push_back ({ ks[c].id, 0.0f }); shi.push_back ({ ks[c].id, 1.0f }); }
                const double d = differ (run (lo, slo), run (hi, shi));
                heard << ks[c].name << " " << juce::String ((int) std::lround (d)) << (c < 7 ? ", " : "");
                all = all && d > -30.0;
            }
            std::printf ("  %s: each of its eight knobs is heard, end to end of its travel (dB of difference: %s)\n", all ? "ok  " : "FAIL", heard.toRawUTF8());
            if (! all) ++bad;
        }

        // MIX at nought is the drums as they came; half way their lows are not hollowed out
        {
            const auto none = run ({ { kDrive, 1.0f } }, { { "mix", 0.0f } }), half = run ({ { kBoom, 0.001f } }, { { "mix", 0.5f } });
            const double lowPlain = tone (plain, 50.0) + tone (plain, 100.0) + tone (plain, 150.0), lowHalf = tone (half, 50.0) + tone (half, 100.0) + tone (half, 150.0);
            const bool ok = std::abs (db (power (none)) - db (power (plain))) < 0.3 && std::abs (db (lowHalf) - db (lowPlain)) < 1.5;
            std::printf ("  %s: MIX at nought is the kick as it came (%+.2f dB), and half way its lows are whole (%+.2f dB at 50, 100 and 150 Hz)\n",
                         ok ? "ok  " : "FAIL", db (power (none)) - db (power (plain)), db (lowHalf) - db (lowPlain));
            if (! ok) ++bad;
        }

        // DRIVE: denser, no hotter. And the same at any level the drums are at.
        {
            const auto d1 = run ({ { kDrive, 1.0f } });
            const double up = db (power (d1)) - db (power (plain)), pk = 20.0 * std::log10 (peak (d1) / peak (plain));
            const auto q0 = run ({}, {}, 0.0f, 0.35f), q1 = run ({ { kDrive, 1.0f } }, {}, 0.0f, 0.35f);
            const double upQuiet = db (power (q1)) - db (power (q0)), quieter = db (power (q0)) - db (power (plain));
            const bool ok = up > 2.0 && pk < 1.5 && pk > -3.0 && std::abs (upQuiet - up) < 1.0;
            std::printf ("  %s: DRIVE makes the kick denser (%+.1f dB of level) and no hotter (its peak %+.1f dB); with the lane %.0f dB down it does the same (%+.1f dB)\n", ok ? "ok  " : "FAIL", up, pk, -quieter, upQuiet);
            if (! ok) ++bad;
            // the compressor comes in with the first third of it, and COMP off leaves saturation alone
            const auto third = run ({ { kDrive, 0.33f } }), thirdDry = run ({ { kDrive, 0.33f } }, { { "comp", 0.0f } });
            const double compDoes = differ (third, thirdDry);
            const bool glue = compDoes > -30.0;
            std::printf ("  %s: the compressor comes in with DRIVE (a third of the way up, COMP on against off: %.0f dB of difference)\n", glue ? "ok  " : "FAIL", compDoes);
            if (! glue) ++bad;
            bool finite = true; double worstPk = 0.0;
            for (float ty : { 0.0f, 0.5f, 1.0f })
            {
                const auto all = run ({ { kDrive, 1.0f }, { kCrunch, 1.0f }, { kPunch, 1.0f }, { kBoom, 1.0f } }, { { "type", ty }, { "boom_decay", 1.0f } });
                for (float v : all) finite = finite && std::isfinite (v);
                finite = finite && peak (all) < 6.0 * peak (plain) && power (all) > power (plain);
                worstPk = std::max (worstPk, peak (all) / peak (plain));
            }
            std::printf ("  %s: with everything full up, in each TYPE, it stays within bounds (its peak %.1f times the kick's at most)\n", finite ? "ok  " : "FAIL", worstPk);
            if (! finite) ++bad;
        }

        // PUNCH, on the clap: either way the hit stands out more against what follows; right the tail is
        // lifted, left it is pulled down. And on the kick it leaves the sub as it was.
        {
            lane = 3;
            const auto mid = run ({}), full = run ({ { kPunch, 1.0f } }), tight = run ({ { kPunch, 0.0f } });
            lane = 0;
            const double tMid = db (upper (mid, 60.0, 220.0)), tFull = db (upper (full, 60.0, 220.0)), tTight = db (upper (tight, 60.0, 220.0));
            const double hMid = db (upper (mid, 0.0, 8.0)), hFull = db (upper (full, 0.0, 8.0)), hTight = db (upper (tight, 0.0, 8.0));
            const auto kFull = run ({ { kPunch, 1.0f } }), kTight = run ({ { kPunch, 0.0f } });
            const double subFull = db (tone (kFull, 50.0)) - db (tone (plain, 50.0)), subTight = db (tone (kTight, 50.0)) - db (tone (plain, 50.0));
            const bool ok = power (mid) > 1.0e-8 && tFull > tMid + 2.0 && tTight < tMid - 3.0 && hFull > hMid + 1.0 && hTight > hMid + 1.0 && std::abs (subFull) < 1.5 && std::abs (subTight) < 1.5;
            std::printf ("  %s: PUNCH right lifts what follows the clap's hit (%+.1f dB above 150 Hz, 60 to 220 ms on), left pulls it down (%+.1f dB); the hit itself comes out either way (%+.1f, %+.1f dB); the kick's sub is left as it was (%+.1f, %+.1f dB at 50 Hz)\n",
                         ok ? "ok  " : "FAIL", tFull - tMid, tTight - tMid, hFull - hMid, hTight - hMid, subFull, subTight);
            if (! ok) ++bad;
        }

        // BOOM: weight at BOOM TUNE, for as long as BOOM DECAY; in the rig's key unless told not to
        {
            const float at50 = (50.0f - 30.0f) / 60.0f, at47 = (47.0f - 30.0f) / 60.0f;
            const auto some = run ({ { kBoom, 1.0f } }, { { "boom_tune", at50 }, { "boom_decay", 0.3f } }), lng = run ({ { kBoom, 1.0f } }, { { "boom_tune", at50 }, { "boom_decay", 1.0f } });
            const double more = db (tone (some, 50.0)) - db (tone (plain, 50.0));
            const double late = db (tone (lng, 50.0, 280.0, 430.0)) - db (tone (some, 50.0, 280.0, 430.0));
            const double loud = db (tone (lng, 50.0)) - db (tone (some, 50.0));
            // KEY of A: the nearest A to 47 Hz is 55
            const auto free = run ({ { kBoom, 1.0f } }, { { "boom_tune", at47 }, { "boom_decay", 1.0f }, { "to_key", 0.0f } }, 10.0f / 12.0f);
            const auto keyed = run ({ { kBoom, 1.0f } }, { { "boom_tune", at47 }, { "boom_decay", 1.0f } }, 10.0f / 12.0f);
            const double freeLean = db (tone (free, 55.0, 250.0, 430.0)) - db (tone (free, 47.0, 250.0, 430.0)), keyedLean = db (tone (keyed, 55.0, 250.0, 430.0)) - db (tone (keyed, 47.0, 250.0, 430.0));
            const bool ok = more > 4.0 && late > 2.0 && loud > -3.0 && keyedLean > freeLean + 6.0;
            std::printf ("  %s: BOOM adds weight at BOOM TUNE (%+.1f dB at 50 Hz); BOOM DECAY keeps it ringing (%+.1f dB there, 280 to 430 ms on) and does not thin it (%+.1f dB over the bar); in the key of A a boom tuned to 47 Hz rings at 55 (55 against 47 Hz: %+.1f dB, from %+.1f with BOOM TO KEY off)\n",
                         ok ? "ok  " : "FAIL", more, late, loud, keyedLean, freeLean);
            if (! ok) ++bad;
        }

        // KEY reaches the sound: the bass moves with it (and so the boom above could)
        {
            auto bass = [&] (float key)
            {
                Engine e; e.prepare (48000.0, block);
                for (int l = 0; l < kLanes; ++l) if (l != 5) e.set (cx::lane (l, cx::kMute), 1.0f, 0.0);
                e.set (cx::kKey, key, 0.0);
                Tap tap; tap.want = 1 + 5;
                for (auto* v : { &tap.lane, &tap.bus, &tap.set }) v->reserve (48000 * 6);
                juce::AudioBuffer<float> b (2, block);
                e.setRunning (true);
                for (int i = 0; i < (int) (4.0 * beat * 48000.0 / block); ++i) e.render (b);
                e.setRecSink (&tap);
                for (int i = 0; i < (int) (8.0 * beat * 48000.0 / block); ++i) e.render (b);
                e.setRecSink (nullptr);
                return tap.lane;
            };
            const auto off = bass (0.0f), inG = bass (8.0f / 12.0f);
            const double d = differ (off, inG);
            const bool ok = power (off) > 1.0e-7 && d > -20.0;
            std::printf ("  %s: KEY moves the bass (OFF against G: %.0f dB of difference)\n", ok ? "ok  " : "FAIL", d);
            if (! ok) ++bad;
        }

        // A lane taken off the bus in its CONFIG goes straight to the mix: the bus has nothing of it to work on
        {
            run ({ { kDrive, 1.0f }, { kBoom, 1.0f } });
            const double on = peak (did);
            run ({ { kDrive, 1.0f }, { kBoom, 1.0f } }, { { "in_1", 0.0f } });
            const double off = peak (did);
            const bool ok = on > 1.0e-3 && off < 1.0e-6;
            std::printf ("  %s: with DRIVE and BOOM up the bus works on the kick (%.3f at most); with KICK put out in its CONFIG it has nothing to work on (%.1e)\n", ok ? "ok  " : "FAIL", on, off);
            if (! ok) ++bad;
        }

        // A snapshot holds the bus: its four knobs travel on a recall, its CONFIG comes with them; a recall can leave it out
        {
            Engine e; Session s (e, false);
            const int drive = e.find ("bus.drive"), boom = e.find ("bus.boom");
            auto cfg = [&] (const char* id) { for (auto& kv : e.getSetup ({ 3, 0, 0 })) if (kv.first == id) return kv.second; return -1.0f; };
            s.userSet (drive, 0.6f); s.userSet (boom, 0.4f);
            e.setSetup ({ 3, 0, 0 }, "type", 1.0f); e.setSetup ({ 3, 0, 0 }, "in_6", 1.0f);
            const auto hot = s.take();
            s.userSet (drive, 0.0f); s.userSet (boom, 0.0f);
            e.setSetup ({ 3, 0, 0 }, "type", 0.0f); e.setSetup ({ 3, 0, 0 }, "in_6", 0.0f);
            const auto cold = s.take();
            s.recall (hot);
            const bool back = drive >= 0 && boom >= 0 && std::abs (e.target (drive) - 0.6f) < 1.0e-4f && std::abs (e.target (boom) - 0.4f) < 1.0e-4f && cfg ("type") > 0.9f && cfg ("in_6") > 0.5f && e.laneOnBus (5);
            Session::Scope notBus; notBus.bus = false;
            s.recall (cold, notBus);
            const bool left = std::abs (e.target (drive) - 0.6f) < 1.0e-4f && cfg ("type") > 0.9f && e.laneOnBus (5);
            s.recall (cold);
            const bool gone = e.target (drive) < 1.0e-4f && cfg ("type") < 0.1f && ! e.laneOnBus (5);
            // a snapshot knob can reach it, and a snapshot from before there was a bus leaves it alone
            const bool reach = Session::reaches (e, drive) && e.control (drive).snapshot && e.control (drive).glides;
            s.userSet (drive, 0.5f);
            Snapshot old = *s.findSnap (cold);
            old.id = 4242; old.hasBus = false; for (int k = 0; k < kBusKnobs; ++k) old.values[(size_t) (cx::kBus + k)] = -1.0f;
            s.library.push_back (old);
            s.recall (4242);
            const bool alone = std::abs (e.target (drive) - 0.5f) < 1.0e-4f;
            const bool ok = back && left && gone && reach && alone;
            std::printf ("  %s: a snapshot holds the drum bus (its knobs and its CONFIG come back; a recall without BUS leaves it; a snapshot knob can reach it; a snapshot from before it leaves it be)\n", ok ? "ok  " : "FAIL");
            if (! ok) { std::printf ("    (back %d left %d gone %d reach %d alone %d)\n", (int) back, (int) left, (int) gone, (int) reach, (int) alone); ++bad; }
        }
    }

    // A snapshot keeps what was playing: the notes that came in from outside (as steps), whether each
    // lane's own sequencer was heard, and the sample a lane held. A recall brings them, or leaves
    // the sequencers be.
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_played_test");
        tmp.deleteRecursively();
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        const int block = 256;
        struct Tap : Engine::RecSink
        {
            std::vector<float> a; int want = 1; bool on = false;
            void begin (int) noexcept override {}
            void track (int idx, const float* l, const float*, int n) noexcept override { if (on && idx == want && a.size() + (size_t) n <= a.capacity()) a.insert (a.end(), l, l + n); }
            void end (int) noexcept override {}
        };
        // what plays in from outside, as a sequencer would: each part goes round every `loop` beats, a few milliseconds late
        struct Note { double at, len; int note, vel; };
        struct Part { int channel; double loop; std::vector<Note> notes; };
        const Part bass { 6, 8.0, { { 0.0, 0.2, 36, 100 }, { 0.75, 0.2, 36, 80 }, { 1.5, 0.45, 39, 110 }, { 2.5, 0.2, 36, 90 }, { 4.0, 0.2, 43, 100 }, { 5.5, 0.2, 41, 100 }, { 6.5, 0.7, 36, 127 } } };
        Part hat { 2, 4.0, {} };
        for (int k = 0; k < 8; ++k) hat.notes.push_back ({ 0.5 * k, 0.1, 60, k % 2 == 0 ? 110 : 70 });
        hat.notes.push_back ({ 3.625, 0.05, 60, 70 });                         // (a second hit in step 15: a ratchet)
        const Part open { 3, 4.0, { { 0.75, 0.2, 60, 100 }, { 2.75, 0.2, 60, 100 } } };       // steps 4 and 12, with the lane's own steps sounding too
        Part wander { 9, 1.0e9, {} };                                           // a part that never comes round
        for (int k = 0; k < 96; ++k) if (seqHash (41, k) > 0.6f) wander.notes.push_back ({ 0.25 * k, 0.2, 48 + (int) (seqHash (7, k) * 12.0f), 100 });
        const std::vector<const Part*> parts = { &bass, &hat, &open, &wander };
        auto playTo = [&] (Engine& e, double toBeat, bool outside)
        {
            juce::AudioBuffer<float> b (2, block);
            const double spb = 48000.0 * 60.0 / e.tempo(), late = 0.002 * e.tempo() / 60.0;
            while (e.songPos() < toBeat - 1.0e-9)
            {
                const double q0 = e.songPos(), q1 = q0 + (double) block / spb;
                if (outside)
                    for (auto* p : parts)
                        for (auto& n : p->notes)
                            for (int end = 0; end < 2; ++end)
                            {
                                const double t = n.at + late + (end == 1 ? n.len : 0.0);
                                const double first = std::ceil ((q0 - t) / p->loop - 1.0e-12) * p->loop + t;
                                if (first < q0 || first >= q1) continue;
                                const int at = juce::jlimit (0, block - 1, (int) std::floor ((first - q0) * spb));
                                e.noteInAt (end == 0 ? juce::MidiMessage::noteOn (p->channel, n.note, (juce::uint8) n.vel) : juce::MidiMessage::noteOff (p->channel, n.note), at);
                            }
                e.render (b);
            }
        };
        auto same = [] (const Pattern& a, const Pattern& b)
        {
            if (a.length != b.length) return false;
            for (int i = 0; i < a.length; ++i)
            {
                const auto& x = a.steps[(size_t) i]; const auto& y = b.steps[(size_t) i];
                if (x.on != y.on || (x.on && (x.note != y.note || x.octave != y.octave || x.ratchet != y.ratchet || std::abs (x.vel - y.vel) > 0.01f || std::abs (x.gate - y.gate) > 0.06f))) return false;
            }
            return true;
        };
        auto lit = [] (const Pattern& p) { juce::String t; for (int i = 0; i < p.length; ++i) if (p.steps[(size_t) i].on) t << (t.isEmpty() ? "" : " ") << (i + 1); return t; };
        auto muted = [] (Engine& e, int l) { return e.target (cx::lane (l, cx::kSeqMute)) > 0.5f; };
        // each sixteenth's level over two bars of a lane's stem
        auto levels = [] (const std::vector<float>& x, double spb)
        {
            std::vector<double> v;
            for (int k = 0; k < 32; ++k)
            {
                const size_t a = (size_t) ((double) k * 0.25 * spb), b = std::min (x.size(), (size_t) ((double) (k + 1) * 0.25 * spb));
                double q = 0.0; for (size_t i = a; i < b; ++i) q += (double) x[i] * x[i];
                v.push_back (10.0 * std::log10 (q / (double) std::max<size_t> (1, b - a) + 1.0e-12));
            }
            return v;
        };
        juce::int64 keptId = 0;
        {
            Engine e; e.prepare (48000.0, block);
            Session s (e, true);
            const double spb = 48000.0 * 60.0 / e.tempo();
            // the kick and the open hat play their own steps; every other lane's own sequencer is muted (the outside plays them)
            for (int l = 0; l < kLanes; ++l) s.userSet (cx::lane (l, cx::kSeqMute), l == 0 || l == 2 ? 0.0f : 1.0f);
            const Pattern kickWas = e.pattern (0), openWas = e.pattern (2), bassOwn = e.pattern (5);
            const int cut = e.find ("bass.cutoff");
            s.userSet (cx::kGlide, 0.0f); s.userSet (cut, 0.3f);
            Tap tap; tap.want = 1 + 5; tap.a.reserve (48000 * 8);
            e.setRecSink (&tap);
            e.setRunning (true);
            playTo (e, 16.0, true);
            tap.on = true; playTo (e, 24.0, true); tap.on = false;             // two bars of the bass as the outside plays it
            const auto bassOut = tap.a; tap.a.clear();
            const auto id = s.take();
            keptId = id;
            s.finishTakes (true);
            const Snapshot snap = *s.findSnap (id);

            // what it kept
            const auto& hb = snap.heard[5]; const auto& hh = snap.heard[1];
            auto step = [] (const Pattern& p, int i) -> const Step& { return p.steps[(size_t) i]; };
            const bool bassKept = snap.hasPlayed && snap.heardOn[5] && hb.length == 32 && lit (hb) == "1 4 7 11 17 23 27"
                               && step (hb, 0).note == 0 && step (hb, 0).octave == 1 && step (hb, 6).note == 3 && step (hb, 16).note == 7 && step (hb, 22).note == 5
                               && std::abs (step (hb, 6).gate - 1.8f) < 0.06f && std::abs (step (hb, 26).gate - 2.8f) < 0.06f && std::abs (step (hb, 26).vel - 1.0f) < 0.01f && std::abs (step (hb, 3).vel - 80.0f / 127.0f) < 0.01f
                               && snap.heardNotes[5].isArray() && snap.heardNotes[5].size() == 7;
            const bool hatKept = snap.heardOn[1] && hh.length == 16 && lit (hh) == "1 3 5 7 9 11 13 15" && step (hh, 14).ratchet == 2 && step (hh, 12).ratchet == 1;
            const bool wanderKept = snap.heardOn[8] && snap.heard[8].length == 64;
            const bool whoPlayed = ! snap.heardOn[0] && ! snap.heardOn[7] && snap.heardOn[2] && ! snap.seqMuted[0] && ! snap.seqMuted[2] && snap.seqMuted[5] && snap.seqMuted[7];
            const bool kept = bassKept && hatKept && wanderKept && whoPlayed;
            std::printf ("  %s: a snapshot keeps what was playing in from outside, as steps: a two-bar bass line (32 steps: %s, its notes, gates and velocities), a hat that goes round in a bar (16 steps: %s, the ratchet in step 15), a part that never comes round (its last four bars), and which lanes' own sequencers were heard\n",
                         kept ? "ok  " : "FAIL", lit (hb).toRawUTF8(), lit (hh).toRawUTF8());
            if (! kept) { std::printf ("    (bass %d hat %d wander %d (%d steps) who %d)\n", (int) bassKept, (int) hatKept, (int) wanderKept, snap.heard[8].length, (int) whoPlayed); ++bad; }

            // the outside goes quiet, the sequencers are wiped and their mutes turned about; a recall with notes puts it all back
            for (int l = 0; l < kLanes; ++l) s.setPattern (l, Pattern());
            for (int l = 0; l < kLanes; ++l) s.userSet (cx::lane (l, cx::kSeqMute), l == 0 || l == 5 || l == 1 ? 1.0f : 0.0f);
            s.userSet (cut, 0.8f);
            s.recall (id);
            Pattern merged = openWas;
            merged.steps[3] = snap.heard[2].steps[3]; merged.steps[11] = snap.heard[2].steps[11];
            const bool given = same (e.pattern (5), hb) && same (e.pattern (1), hh) && same (e.pattern (0), kickWas) && same (e.pattern (2), merged) && same (e.pattern (7), snap.patterns[7]);
            const bool mutes = ! muted (e, 0) && ! muted (e, 1) && ! muted (e, 2) && ! muted (e, 5) && muted (e, 7) && ! muted (e, 8) && muted (e, 9);
            playTo (e, 32.0, false);                                            // (the recalled steps are taken up on the bar line)
            tap.on = true; playTo (e, 40.0, false); tap.on = false;             // two bars of the bass from its own sequencer
            const auto bassIn = tap.a; tap.a.clear();
            const auto was = levels (bassOut, spb), is = levels (bassIn, spb);
            double off = 0.0; int sounding = 0;
            // (down to 40 dB under full: the outside's notes come a few milliseconds late, so the edge of a hit falls in the sixteenth before it one way and not the other)
            for (int k = 0; k < 32; ++k) if (was[(size_t) k] > -40.0 || is[(size_t) k] > -40.0) { off += std::abs (std::max (-40.0, was[(size_t) k]) - std::max (-40.0, is[(size_t) k])); ++sounding; }
            off /= std::max (1, sounding);
            const bool sounds = bassOut.size() > 48000 && sounding >= 7 && off < 2.5;
            if (! sounds) { for (int k = 0; k < 32; ++k) std::printf ("%s%.0f/%.0f", k == 0 ? "    (" : " ", was[(size_t) k], is[(size_t) k]); std::printf (")\n"); }
            const bool back = given && mutes && sounds && std::abs (e.target (cut) - 0.3f) < 1.0e-4f;
            std::printf ("  %s: recalled with NOTES lit and nothing playing in: each lane's sequencer has what the lane was playing (the bass line, the hat, the kick's own steps, the open hat's own steps with the outside's two among them), its mute set to match, and the bass sounds as it did (over two bars, a sixteenth's level %.2f dB off on average)\n",
                         back ? "ok  " : "FAIL", off);
            if (! back) { std::printf ("    (given %d mutes %d sounds %d: %d sixteenths with sound; the bass has steps %s)\n", (int) given, (int) mutes, (int) sounds, sounding, lit (e.pattern (5)).toRawUTF8()); ++bad; }

            // NOTES out: the sound alone. Every sequencer, its steps and its mute, stays as it is
            s.setRecallNotes (false);
            Pattern mine; mine.steps[1].on = true; mine.steps[1].octave = 1;
            s.setPattern (5, mine); s.userSet (cx::lane (5, cx::kSeqMute), 1.0f); s.userSet (cx::lane (7, cx::kSeqMute), 0.0f);
            s.userSet (cut, 0.8f);
            s.recall (id);
            const bool soundOnly = same (e.pattern (5), mine) && muted (e, 5) && ! muted (e, 7) && std::abs (e.target (cut) - 0.3f) < 1.0e-4f;
            // a snapshot knob's press and a part of a recall go by it too; a snapshot from before any of this gives a lane its own steps and leaves the mutes
            s.setRecallNotes (true);
            Snapshot old = snap; old.id = 4243; old.hasPlayed = false;
            s.library.push_back (old);
            s.userSet (cx::lane (5, cx::kSeqMute), 1.0f);
            s.recall (4243);
            const bool asBefore = same (e.pattern (5), bassOwn) && muted (e, 5);
            Session::Scope part; part.lane.fill (false); part.lane[5] = true; part.returns = false; part.bus = false; part.master = false; part.notes = false;
            s.setPattern (5, mine);
            s.recall (id, part);
            const bool partly = same (e.pattern (5), mine);
            const bool ok = soundOnly && asBefore && partly;
            std::printf ("  %s: with NOTES out a recall is the sound alone (the knobs come, the bass's steps and its sequencer's mute stay as they were); a snapshot from before notes were kept gives a lane its own steps and leaves the mutes, as it always did\n", ok ? "ok  " : "FAIL");
            if (! ok) { std::printf ("    (sound only %d as before %d partly %d)\n", (int) soundOnly, (int) asBefore, (int) partly); ++bad; }

            // the sample a lane held: a recall puts it back
            auto wavOf = [&] (const char* name, double hz)
            {
                const auto f = tmp.getChildFile (name);
                juce::AudioBuffer<float> tone (1, 24000);
                for (int i = 0; i < 24000; ++i) tone.setSample (0, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0));
                juce::WavAudioFormat wav;
                if (auto out = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                    if (auto* w = wav.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0)) { out.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (tone, 0, 24000); }
                return f;
            };
            const auto fa = wavOf ("a.wav", 220.0), fb = wavOf ("b.wav", 330.0);
            const bool loaded = e.loadFile (10, fa);
            const auto withA = s.take();
            s.finishTakes (true);
            e.loadFile (10, fb);
            auto holds = [&] (const char* stem) { return juce::File (e.laneFile (10)).getFileName().startsWith (juce::String (stem) + "."); };      // (the library's own copy of it)
            const bool holdsB = holds ("b");
            s.recall (withA);
            const bool holdsA = holds ("a") && s.findSnap (withA)->laneFiles[10] == e.laneFile (10);
            e.loadFile (10, fb);
            s.recall (4243);                                                    // (one from before samples were kept... it says the lane had none: left as it is)
            const bool left = holds ("b");
            const bool files = loaded && holdsB && holdsA && left;
            std::printf ("  %s: a snapshot keeps the sample a lane held: taken with one phrase on VOICE and recalled with another there, the first is back; a snapshot that names none leaves the lane's as it is\n", files ? "ok  " : "FAIL");
            if (! files) { std::printf ("    (loaded %d B %d A %d left %d)\n", (int) loaded, (int) holdsB, (int) holdsA, (int) left); ++bad; }

            // and all of it through the file
            s.setRecallNotes (false);
            s.waitForClips();
            s.save();
            e.setRecSink (nullptr);
        }
        {
            Engine e2; Session s2 (e2, true);
            const Snapshot* r = s2.findSnap (keptId);
            const Snapshot* withFile = nullptr;
            for (auto& x : s2.library) if (x.hasFiles && juce::File (x.laneFiles[10]).getFileName().startsWith ("a.") && juce::File (x.laneFiles[10]).existsAsFile()) withFile = &x;
            const bool ok = r != nullptr && r->hasPlayed && r->heardOn[5] && r->heard[5].length == 32 && lit (r->heard[5]) == "1 4 7 11 17 23 27" && r->heard[5].steps[6].note == 3 && std::abs (r->heard[5].steps[26].gate - 2.8f) < 0.06f
                         && r->heard[1].steps[14].ratchet == 2 && r->seqMuted[5] && ! r->seqMuted[0] && r->heardNotes[5].size() == 7 && withFile != nullptr && ! s2.recallNotes;
            std::printf ("  %s: saved and opened again, a snapshot still holds what was playing (the bass's steps and its notes as they came, the ratchet, the mutes) and the sample; NOTES is as it was left\n", ok ? "ok  " : "FAIL");
            if (! ok) ++bad;
            s2.waitForClips();
        }
        Session::stateFileForTests = juce::File();
        tmp.deleteRecursively();
    }

    // The library holds its own samples: every file given to the rig is copied into samples/, kept
    // once, written from the library folder down, and gone with the last snapshot that held it. A
    // session from before is brought over. The library's folder and the takes' folder can be chosen.
    {
        using juce::var;
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_places_test");
        tmp.deleteRecursively();
        const auto home = tmp.getChildFile ("home"), outside = tmp.getChildFile ("my sounds");
        home.createDirectory(); outside.createDirectory();
        Session::homeForTests = home;
        Session::settlePlaces (true);
        auto wavOf = [&] (const char* name, double hz)
        {
            const auto f = outside.getChildFile (name);
            juce::AudioBuffer<float> tone (1, 12000);
            for (int i = 0; i < 12000; ++i) tone.setSample (0, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0));
            juce::WavAudioFormat wav;
            if (auto out = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                if (auto* w = wav.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0)) { out.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (tone, 0, 12000); }
            return f;
        };
        const auto fa = wavOf ("Vox A.wav", 220.0), fb = wavOf ("vox_b.wav", 330.0), fc = wavOf ("c.wav", 440.0);
        auto copies = [] (const juce::File& lib) { return lib.getChildFile ("samples").findChildFiles (juce::File::findFiles, true, "*.wav").size(); };
        auto copyOf = [] (Engine& e, const char* stem) { const juce::File f (e.laneFile (10)); return f.getFileName() == juce::String (stem) + ".wav" && f.existsAsFile() ? f : juce::File(); };
        juce::int64 idA = 0, idB = 0;
        {
            Engine e; e.prepare (48000.0, 256);
            Session s (e, true);
            const bool loaded = e.loadFile (10, fa);
            const juce::File ca = copyOf (e, "Vox A");
            e.loadFile (10, fa);                                                // the same file again: kept once
            const bool once = copies (home) == 1 && s.samples.size() == 1 && s.samples[0].from == fa.getFullPathName() && s.samples[0].hash.length() == 64;
            const juce::String in = ca.getParentDirectory().getFileName();          // (a folder named for what is in the file)
            const bool inLibrary = ca.isAChildOf (home.getChildFile ("samples")) && fa.existsAsFile() && ca.getSize() == fa.getSize() && ca.getFileName() == "Vox A.wav"
                                && in.length() == 8 && in.containsOnly ("0123456789abcdef") && s.samples[0].hash.startsWith (in);
            idA = s.take(); s.finishTakes (true);
            e.loadFile (10, fb);
            idB = s.take(); s.finishTakes (true);
            s.waitForClips(); s.save();
            const juce::String text = Session::stateFile().loadFileAsString();
            const var root = juce::JSON::parse (text);
            const var lib = root["library"];
            juce::String written;
            for (int i = 0; i < lib.size(); ++i) if ((juce::int64) lib[i]["id"] == idA) written = lib[i]["laneFiles"][10].toString();
            const bool relative = written == "samples/" + in + "/Vox A.wav" && root["files"][10].toString().startsWith ("samples/") && root["files"][10].toString().endsWith ("/vox_b.wav") && Session::stateFile().getParentDirectory() == home;
            // the files they came from can go: the library has its own
            fa.deleteFile(); fb.deleteFile();
            s.recall (idA);
            const bool fromCopy = copyOf (e, "Vox A") == ca;
            const bool ok = loaded && once && inLibrary && relative && fromCopy;
            const bool knows = s.cameFrom (ca.getFullPathName()) == outside.getChildFile ("Vox A.wav").getFullPathName();
            std::printf ("  %s: a sample given to a lane is copied into the library's samples folder under its own name, once however often it is given; the file it came from is untouched and can go; the session writes it from the library folder down (%s) and knows where it came from\n",
                         ok && knows ? "ok  " : "FAIL", written.toRawUTF8());
            if (! knows) ++bad;
            if (! ok) { std::printf ("    (loaded %d once %d in the library %d relative %d from the copy %d)\n", (int) loaded, (int) once, (int) inLibrary, (int) relative, (int) fromCopy); ++bad; }

            // deleting a snapshot takes with it the samples only it held
            s.recall (idB);                                                     // (the rig holds B now; A is the first snapshot's alone)
            const juce::File cb = copyOf (e, "vox_b");
            s.remove (idA);
            const bool aGone = ! ca.existsAsFile() && ! ca.getParentDirectory().exists() && cb.existsAsFile() && s.samples.size() == 1;
            s.remove (idB);                                                     // B is still on the lane: it stays
            const bool bStays = cb.existsAsFile() && s.samples.size() == 1;
            e.loadFile (10, fc);                                                // the lane is given another; B is nobody's now, and goes at the next clearing
            const bool bWaits = cb.existsAsFile();
            const int swept = s.sweepSamples();
            const bool bGone = swept == 1 && ! cb.existsAsFile() && copies (home) == 1 && s.samples.size() == 1;
            const bool gone = aGone && bStays && bWaits && bGone;
            std::printf ("  %s: deleting a snapshot removes the sample only it held, and leaves one a lane or another snapshot still holds; a sample nothing holds any more goes at the next clearing\n", gone ? "ok  " : "FAIL");
            if (! gone) { std::printf ("    (A gone %d, B stays %d, waits %d, gone %d: %d swept, %d copies)\n", (int) aGone, (int) bStays, (int) bWaits, (int) bGone, swept, copies (home)); ++bad; }
            idA = s.take(); s.finishTakes (true);                               // one snapshot, holding C
            s.waitForClips();
        }

        // A session from before copies were kept: its files written whole, wherever they were
        {
            auto text = Session::stateFile().loadFileAsString();
            var root = juce::JSON::parse (text);
            const juce::String rel = root["files"][10].toString(), whole = fc.getFullPathName();
            text = text.replace ("\"" + rel + "\"", juce::JSON::toString (var (whole)));
            var old = juce::JSON::parse (text);
            old.getDynamicObject()->removeProperty ("samples");
            Session::stateFile().replaceWithText (juce::JSON::toString (old));
            home.getChildFile ("samples").deleteRecursively();
            const bool wasWhole = rel.startsWith ("samples/") && rel.endsWith ("/c.wav") && ! text.contains (rel) && copies (home) == 0;
            juce::String kept;
            {
                Engine e; e.prepare (48000.0, 256);
                Session s (e, true);
                const juce::File cc = copyOf (e, "c");
                const bool brought = cc.isAChildOf (home.getChildFile ("samples")) && s.samples.size() == 1 && s.samples[0].from == whole && s.findSnap (idA) != nullptr && s.findSnap (idA)->laneFiles[10] == cc.getFullPathName();
                s.save();
                const var again = juce::JSON::parse (Session::stateFile());
                for (int i = 0; i < again["library"].size(); ++i) if ((juce::int64) again["library"][i]["id"] == idA) kept = again["library"][i]["laneFiles"][10].toString();
                fc.deleteFile();                                                // and now the file it came from goes
                const bool ok = wasWhole && brought && kept == whole && again["files"][10].toString() == rel;
                std::printf ("  %s: a session from before copies were kept, its samples written by whole path: opened, each is copied in and read from the copy; the snapshot is written back as it was (%s) and found by where it came from\n", ok ? "ok  " : "FAIL", kept.toRawUTF8());
                if (! ok) { std::printf ("    (was whole %d brought %d kept as it was %d)\n", (int) wasWhole, (int) brought, (int) (kept == whole)); ++bad; }
            }
            {
                Engine e; e.prepare (48000.0, 256);
                Session s (e, true);
                e.loadFile (10, wavOf ("d.wav", 550.0));
                s.recall (idA);
                const bool still = copyOf (e, "c") != juce::File();
                std::printf ("  %s: with that file gone from where it was, the old snapshot still brings its sample back, from the library's copy\n", still ? "ok  " : "FAIL");
                if (! still) ++bad;
                s.waitForClips();
            }
        }

        // The library is moved: chosen in SETUP, taken up at the next start, copied if there is none there
        {
            const auto drive = tmp.getChildFile ("drive");
            drive.createDirectory();
            drive.getChildFile ("something else.txt").replaceWithText ("not SF-000's");
            const juce::String msg = Session::chooseLibrary (drive);
            const bool pending = Session::places().next.isNotEmpty() && Session::libraryDir() == home && msg.contains ("COPIED");
            Session::settlePlaces (true);                                       // the next start
            const auto lib = drive.getChildFile ("SF-000");
            const bool moved = Session::libraryDir() == lib && lib.getChildFile ("session.json").existsAsFile() && copies (lib) >= 1 && Session::places().note.contains ("NOW IN") && Session::places().next.isEmpty()
                            && home.getChildFile ("session.json").existsAsFile();
            home.getChildFile ("session.json").deleteFile(); home.getChildFile ("samples").deleteRecursively(); home.getChildFile ("clips").deleteRecursively();      // the old one is cleared away
            bool plays = false;
            {
                Engine e; e.prepare (48000.0, 256);
                Session s (e, true);
                e.loadFile (10, wavOf ("e.wav", 660.0));
                s.recall (idA);
                const juce::File cc = copyOf (e, "c");
                plays = cc.isAChildOf (lib.getChildFile ("samples")) && s.findSnap (idA) != nullptr;
                s.waitForClips();
            }
            // the settings that are this machine's own stayed at home
            const bool settingsHome = Session::homeFile ("places.json").existsAsFile() && Session::homeFile ("midi.json").getParentDirectory() == home && Online::setupFile().getParentDirectory() == home && Session::homeDir() != Session::libraryDir();
            // its drive unplugged: it stays chosen, the one on this machine is used this time, and it says so
            lib.moveFileTo (tmp.getChildFile ("unplugged"));
            Session::settlePlaces (true);
            const bool missing = Session::places().libraryMissing && Session::libraryDir() == home && Session::places().note.contains ("NOT THERE") && Session::places().library == lib.getFullPathName();
            tmp.getChildFile ("unplugged").moveFileTo (lib);
            Session::settlePlaces (true);
            const bool backAgain = ! Session::places().libraryMissing && Session::libraryDir() == lib;
            const bool ok = pending && moved && plays && settingsHome && missing && backAgain;
            std::printf ("  %s: a folder chosen for the library is taken up at the next start: the library is copied into a folder of its own there (SF-000, the folder not being empty) and plays from it with the old one gone; this machine's settings stay where they were; with the drive unplugged SF-000 says so and uses the one on this machine, and takes the chosen one up again when it is back\n", ok ? "ok  " : "FAIL");
            if (! ok) { std::printf ("    (pending %d moved %d plays %d settings %d missing %d back %d: %s)\n", (int) pending, (int) moved, (int) plays, (int) settingsHome, (int) missing, (int) backAgain, msg.toRawUTF8()); ++bad; }
        }

        // Takes go to the folder set for them, or to the usual one when that is not there
        {
            Engine e; e.prepare (48000.0, 128);
            const auto takes = tmp.getChildFile ("takes"), usual = tmp.getChildFile ("usual");
            takes.createDirectory();
            const juce::String none = Session::chooseRecordings (takes);
            const bool set = none.isEmpty() && Session::recordingsDir() == takes;
            juce::AudioBuffer<float> b (2, 128);
            Recorder rec (e);
            rec.usual = usual; rec.root = Session::recordingsDir();
            const juce::String e1 = rec.start();
            for (int i = 0; i < 40; ++i) e.render (b);
            const bool there = e1.isEmpty() && rec.folder().getParentDirectory() == takes;
            rec.stop();
            takes.deleteRecursively();                                          // its drive goes
            const juce::String e2 = rec.start();
            for (int i = 0; i < 40; ++i) e.render (b);
            const bool fell = e2.isEmpty() && rec.folder().getParentDirectory() == usual && rec.note().contains ("NOT THERE");
            rec.stop();
            const bool ok = set && there && fell;
            std::printf ("  %s: takes go to the folder chosen for them; with that folder gone a take is still made, in the usual place, and says so\n", ok ? "ok  " : "FAIL");
            if (! ok) { std::printf ("    (set %d there %d fell back %d: %s %s)\n", (int) set, (int) there, (int) fell, e1.toRawUTF8(), e2.toRawUTF8()); ++bad; }
        }
        Session::homeForTests = juce::File();
        Session::settlePlaces (true);
        tmp.deleteRecursively();
    }

    pocketsTest (bad);
    convolutionBandsTest (bad);
    knobMotionTest (bad);
    arrangementTest (bad);

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
        e.set (cx::kBus, 0.5f, 0.0);                           // the drum bus at work: what it does is one of the parts
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
        std::printf ("  %s: ten lanes, the two returns and what the drum bus does kept in 32-bit float, each %d samples of silence longer; the lane that played nothing is not kept, and take.txt says so\n",
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
        std::printf ("  %s: the lanes, the returns and what the drum bus does, added together, line up with the set (best fit %d samples off)\n", lined ? "ok  " : "FAIL", bestLag);
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
                    // an effect has the same two: a DELAY on the kick, the second knob of its panel moved by a sine
                    {
                        while (e7.insertType (0, 0) != 0) s7.stepInsert (0, 0);
                        const Engine::Target fx { 1, 0, 0 };
                        auto* fp = e7.processorFor (fx);
                        const auto& cell = effects()[0].cells[1];
                        const int pi = fp->paramIndex (cell.param);
                        auto fxFind = [&] (const juce::String& id) { for (auto& sp : e7.setupParams (fx)) if (sp.id == id) return sp; return Engine::SetupParam(); };
                        auto fxChoose = [&] (const juce::String& id, const juce::String& name)
                        {
                            const auto sp = fxFind (id);
                            const int at = sp.choices.indexOf (name);
                            if (at >= 0 && sp.choices.size() > 1) e7.setSetup (fx, id, (float) at / (float) (sp.choices.size() - 1));
                            return at >= 0;
                        };
                        auto fxSpread = [&] (int blocks)
                        {
                            float lo = 1.0e9f, hi = -1.0e9f;
                            for (int n7 = 0; n7 < blocks; ++n7) { e7.render (b7); const float v = fp->param (pi)->convertTo0to1 (fp->currentValue (pi)); lo = std::min (lo, v); hi = std::max (hi, v); }
                            return hi - lo;
                        };
                        const auto tg = fxFind ("m1_target");
                        const bool there = tg.group == "MOTION 1" && tg.choices.size() > 2 && tg.choices[1] == juce::String (cell.label) && fxFind ("m1_src").choices.contains ("INPUT");      // (TIME, its first knob, is a list of note values: not something a wave can move)
                        const float fxStill = fxSpread (300);
                        const bool fxSet = fxChoose ("m1_src", "SINE") && fxChoose ("m1_rate", "1/4") && fxChoose ("m1_target", cell.label);
                        e7.setSetup (fx, "m1_depth", 1.0f);
                        const float fxMoving = fxSpread (800);
                        const auto keep = s7.take(); s7.finishTakes (true);
                        e7.setSetup (fx, "m1_src", 0.0f);
                        s7.recall (keep);
                        for (int n7 = 0; n7 < 300; ++n7) e7.render (b7);
                        fp = e7.processorFor (fx);
                        const bool fxOk = there && fxSet && fxStill < 0.01f && fxMoving > 0.3f && fp != nullptr && fxFind ("m1_src").text.toUpperCase() == "SINE";
                        std::printf ("  %s: MOTION on an effect: a sine on a DELAY's %s moves it (%.2f of its range still, %.2f with it), INPUT is among its waves, and a snapshot brings it back\n", fxOk ? "ok  " : "FAIL", cell.label, fxStill, fxMoving);
                        if (! fxOk) { ++bad; std::printf ("    (there %d set %d wave now %s; targets: %s)\n", (int) there, (int) fxSet, fxFind ("m1_src").text.toRawUTF8(), tg.choices.joinIntoString (", ").toRawUTF8()); }
                    }
                    // what a snapshot holds of MOTION, everywhere it can be set: a lane's, an insert's, a return's, to the file and back
                    {
                        e7.setSetup (bass, "m1_src", 1.0f / 11.0f);                 // the bass: MOTION 1 a sine again
                        e7.setSetup ({ 2, 0, 0 }, "m2_src", 3.0f / 11.0f);          // return A: MOTION 2 a square (the kick's DELAY has its sine from above)
                        const auto all = s7.take(); s7.finishTakes (true);
                        const auto* sn = s7.findSnap (all);
                        auto has = [] (const Engine::SetupState& st, const char* id) { for (auto& kv : st) if (kv.first == id) return kv.second; return -1.0f; };
                        const bool lane = sn != nullptr && std::abs (has (sn->setup[5], "m1_src") - 1.0f / 11.0f) < 1.0e-4f && has (sn->setup[5], "m1_depth") >= 0.0f && has (sn->setup[5], "m2_target") >= 0.0f;
                        const bool ret = sn != nullptr && std::abs (has (sn->retSetup[0], "m2_src") - 3.0f / 11.0f) < 1.0e-4f;
                        const bool ins = sn != nullptr && std::abs (has (sn->insSetup[0][0], "m1_src") - 1.0f / 11.0f) < 1.0e-4f;
                        // (as the session file and the library on the web are given it: by name, like every other CONFIG value)
                        const juce::String asWritten = sn != nullptr ? juce::JSON::toString (s7.written (*sn)) : juce::String();
                        const bool file = asWritten.contains ("m1_src") && asWritten.contains ("m1_rate") && asWritten.contains ("m1_target") && asWritten.contains ("m1_depth") && asWritten.contains ("m2_src");
                        // its random waves: drawn from the same numbers on the same lane, whichever voice plays (a recall crosses it over)
                        const auto seedWas = e7.processorFor (bass)->motionSeedNow();
                        e7.forkVoice (5, 0.0);
                        const bool sameWave = e7.processorFor (bass)->motionSeedNow() == seedWas && seedWas == Engine::motionSeedFor (bass)
                                           && Engine::motionSeedFor ({ 0, 6, 0 }) != seedWas && Engine::motionSeedFor ({ 1, 5, 0 }) != seedWas;
                        e7.setSetup ({ 2, 0, 0 }, "m2_src", 0.0f);
                        const bool ok8 = lane && ret && ins && file && sameWave;
                        std::printf ("  %s: a snapshot holds MOTION wherever it is set (a lane's, an insert's, a return's) and writes it to the session file; and a lane's random waves are the same waves after its voice has crossed over\n", ok8 ? "ok  " : "FAIL");
                        if (! ok8) { ++bad; std::printf ("    (lane %d return %d insert %d file %d wave %d)\n", (int) lane, (int) ret, (int) ins, (int) file, (int) sameWave); }
                    }
                    // a recall of part of a snapshot: what is chosen comes from it, the rest stays as it is
                    {
                        Engine e9; e9.prepare (48000.0, 128);
                        Session s9 (e9, false);
                        const int punch = cx::lane (0, 5), cut = cx::lane (5, 6), dub = cx::kRet + 1;
                        auto near = [&] (int ctl, float v) { return std::abs (e9.target (ctl) - v) < 1.0e-4f; };
                        s9.userSet (cx::kGlide, 0.0f);
                        s9.userSet (punch, 0.2f); s9.userSet (cut, 0.2f); s9.userSet (dub, 0.2f); s9.userSet (cx::kKey, 0.0f);
                        const auto pat0 = e9.pattern (0);
                        const auto a9 = s9.take();
                        s9.userSet (punch, 0.8f); s9.userSet (cut, 0.8f); s9.userSet (dub, 0.8f); s9.userSet (cx::kKey, 0.5f);
                        s9.pressGenerator (0, 0);                                       // (and the kick's steps written anew)
                        const auto b9 = s9.take();
                        s9.finishTakes (true);
                        s9.recall (a9);
                        const bool whole = near (punch, 0.2f) && near (cut, 0.2f) && near (dub, 0.2f) && near (cx::kKey, 0.0f);
                        Session::Scope bassOnly; bassOnly.lane.fill (false); bassOnly.lane[5] = true; bassOnly.returns = false; bassOnly.master = false;
                        s9.recall (b9, bassOnly);
                        const bool part = near (punch, 0.2f) && near (cut, 0.8f) && near (dub, 0.2f) && near (cx::kKey, 0.0f);
                        // with a snapshot knob turned, a lane left out stays where the knob has it
                        s9.recall (a9);
                        s9.knobs[0].snaps = { a9, b9 };
                        s9.turnSnapKnob (0, 0.5f);
                        const float punchWas = e9.target (punch);
                        s9.recall (b9, bassOnly);
                        const bool underKnob = std::abs (punchWas - 0.5f) < 0.02f && near (punch, punchWas);
                        s9.turnSnapKnob (0, 0.0f);
                        s9.recall (b9);
                        const bool again = near (punch, 0.8f) && near (cut, 0.8f) && near (dub, 0.8f) && near (cx::kKey, 0.5f);
                        const bool ok9 = whole && part && underKnob && again;
                        std::printf ("  %s: a recall of part of a snapshot (the bass alone): the bass takes the snapshot's place, the kick, the returns and the key stay as they are, under a turned snapshot knob too; a plain recall takes all of it\n", ok9 ? "ok  " : "FAIL");
                        if (! ok9) { ++bad; std::printf ("    (whole %d part %d under the knob %d again %d: punch %.2f cut %.2f dub %.2f tempo %.1f)\n", (int) whole, (int) part, (int) underKnob, (int) again, e9.target (punch), e9.target (cut), e9.target (dub), (double) e9.target (cx::kKey)); }
                        juce::ignoreUnused (pat0);
                    }
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
        ses.recall (shot);                          // taken while it was muted, and nothing was playing the kick from outside: the lane was silent
        audio (4);
        const bool mutedAgain = e.target (sm) > 0.5f;
        click (at); audio (4);
        ses.setRecallNotes (false);
        ses.recall (shot);
        audio (4);
        const bool leftAlone = e.target (sm) < 0.5f;
        ses.setRecallNotes (true);
        check (found == 1 && lit && back && kickOn > 1e-3 && kickOff < kickOn * 1e-4 && hatOff > hatOn * 0.5,
               "the sequencer's M key stops the kick's steps and leaves the hat's playing; a second press brings them back");
        check (mutedAgain && leftAlone, "a recall with NOTES lit sets it as the snapshot had it (taken while the kick's steps were muted and nothing played it: muted again); with NOTES out a recall leaves it alone");
        ses.selectLane (7); draw();
    }

    // 2. everything, on every page: a click, then a drag on what turns
    int clicks = 0;
    for (auto page : { Screen::pMain, Screen::pLibrary, Screen::pMacros, Screen::pPockets })
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
    ses.learnDrop();        // (LEARN was among the keys clicked: whatever it was left teaching is dropped)
    for (int i = cx::kMot; i < cx::kSnap; ++i) ses.userSet (i, ses.engine.control (i).def);       // (and MOTION's knobs were among those dragged: off again)
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
    // what is in front: a press on a module's own plate off its controls, TAB, C, and what a controller asks for
    {
        sc.setPage (Screen::pMain); sc.hideConfig(); sc.testTick(); draw();
        // a place on a panel that no control of it is on
        auto bare = [&] (std::function<bool (const Screen::TestHit&)> is)
        {
            const auto hits = sc.testHits();
            for (size_t i = 0; i < hits.size(); ++i)
            {
                if (! is (hits[i])) continue;
                for (float y = hits[i].r.getY() + 6.0f; y < hits[i].r.getBottom(); y += 9.0f)
                    for (float x = hits[i].r.getX() + 6.0f; x < hits[i].r.getRight(); x += 9.0f)
                    {
                        size_t top = hits.size();
                        for (size_t k = hits.size(); k-- > 0;) if (hits[k].r.contains (x, y)) { top = k; break; }
                        if (top == i) return juce::Point<float> (x, y);
                    }
            }
            return juce::Point<float>();
        };
        using S = Session;
        click (bare ([] (const Screen::TestHit& h) { return Screen::testIsLanePanel (h, 2); })); draw();
        const bool lane = ses.activeLane == 2 && ses.front.kind == S::fLane;
        click (bare ([] (const Screen::TestHit& h) { return Screen::testIsFront (h, S::fInsert, 1); })); draw();
        const bool ins = ses.front == S::Front { S::fInsert, 1 } && ses.activeLane == 2;
        click (bare ([] (const Screen::TestHit& h) { return Screen::testIsFront (h, S::fReturns, 0); })); draw();
        const bool ret = ses.front.kind == S::fReturns;
        click (bare ([] (const Screen::TestHit& h) { return Screen::testIsFront (h, S::fMaster, 0); })); draw();
        const bool mas = ses.front.kind == S::fMaster;
        click (bare ([] (const Screen::TestHit& h) { return Screen::testIsFront (h, S::fSnap, 0); })); draw();
        const bool snp = ses.front.kind == S::fSnap;
        click (bare ([] (const Screen::TestHit& h) { return Screen::testIsFront (h, S::fSeq, 0); })); draw();
        const bool seq = ses.front.kind == S::fSeq && ses.activeLane == 2 && ses.frontBy == 0;
        check (lane && ins && ret && mas && snp && seq, "a press on a module's plate, off its controls, brings it to the front: a lane (it is the active one), one of its inserts, the returns, the master, the snapshot knobs, the sequencer strip");

        // (a rig with an effect in the kick's first and third inserts and nowhere else, put back afterwards)
        std::array<std::array<int, kSlots>, kLanes> insertsWere;
        for (int l = 0; l < kLanes; ++l) for (int sl = 0; sl < kSlots; ++sl) { insertsWere[(size_t) l][(size_t) sl] = e.insertType (l, sl); e.setInsertType (l, sl, (l == 0 && sl != 1) ? 0 : -1); }
        const juce::KeyPress tab (juce::KeyPress::tabKey), back (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);
        const auto all = ses.stops();
        sc.keyPressed (tab);
        const bool end = (int) all.size() == kLanes + 2 + 4 && S::stopIndex (all, ses.stopNow()) == (int) all.size() - 1;      // (the last of them: it stays)
        sc.keyPressed (back); sc.keyPressed (back);
        const bool backTwo = ses.front.kind == S::fReturns;
        for (size_t k = 0; k < all.size(); ++k) sc.keyPressed (back);
        const bool first = ses.stopNow() == S::Stop { S::fLane, 0, 0 } && ses.activeLane == 0;
        sc.keyPressed (tab);
        const bool itsFirst = ses.stopNow() == S::Stop { S::fInsert, 0, 0 };
        sc.keyPressed (tab);
        const bool itsThird = ses.stopNow() == S::Stop { S::fInsert, 0, 2 } && ses.activeLane == 0;       // (its second has nothing in it: no stop)
        sc.keyPressed (tab);
        const bool nextLane = ses.stopNow() == S::Stop { S::fLane, 1, 0 };
        sc.keyPressed (tab);
        const bool laneAfter = ses.stopNow() == S::Stop { S::fLane, 2, 0 };                               // (a lane with no inserts: straight on to the next)
        ses.bringStop ({ S::fInsert, 0, 2 }, 1);
        const bool otherLanes = ses.activeLane == 0 && ses.front == S::Front { S::fInsert, 2 };          // (an insert chosen from another lane: its lane is the active one)
        ses.bringLane (10, 1); sc.keyPressed (tab);
        const bool thenSnap = ses.front.kind == S::fSnap && ses.activeLane == 10;
        check (end && backTwo && first && itsFirst && itsThird && nextLane && laneAfter && otherLanes && thenSnap && ses.frontBy == 1,
               "TAB brings the next to the front, SHIFT-TAB the one before: a lane, those of its inserts that have an effect in them, the next lane; after the lanes the snapshot knobs, the returns, the master, the sequencer strip");
        ses.bringLane (5, 1);

        sc.keyPressed (juce::KeyPress ('c', juce::ModifierKeys(), 'c')); sc.testTick(); draw();
        const bool opened = ses.cfgOpen && ses.cfg.kind == 0 && ses.cfg.lane == 5 && ! sc.testConfigRect().isEmpty();
        sc.keyPressed (tab); sc.testTick(); draw();
        const bool along = ses.cfgOpen && ses.cfg.lane == 6 && ses.activeLane == 6;
        ses.bring ({ S::fInsert, 2 }, 1); sc.testTick(); draw();
        const bool toInsert = ses.cfgOpen && ses.cfg.kind == 1 && ses.cfg.lane == 6 && ses.cfg.slot == 2;
        ses.bring ({ S::fMaster, 0 }, 1); sc.testTick(); draw();
        const bool toMaster = ses.cfgOpen && ses.cfg.kind == 3;         // (the master's CONFIG is its drum bus's)
        int busKnobs = 0, laneKeys = 0;
        for (auto& h : sc.testHits()) for (int k = 0; k < kBusKnobs; ++k) if (Screen::testIsControl (h, cx::kBus + k)) ++busKnobs;
        for (auto& sp : ses.configList()) if (sp.group == "LANES") ++laneKeys;
        ses.bring ({ S::fSeq, 0 }, 1); sc.testTick(); draw();
        const bool shutBySeq = ! ses.cfgOpen;
        // the master's own C key, under the mouse
        {
            juce::Point<float> at; bool found = false;
            for (auto& h : sc.testHits()) if (Screen::testIsBusConfigKey (h)) { at = h.r.getCentre(); found = true; }
            if (found) { click (at); sc.testTick(); draw(); }
        }
        const bool byKey = ses.cfgOpen && ses.cfg.kind == 3 && ! sc.testConfigRect().isEmpty();
        if (byKey) { ses.openConfig ({ 3, 0, 0 }); sc.testTick(); draw(); }
        const bool shutByMaster = toMaster && busKnobs == kBusKnobs && laneKeys == kLanes && shutBySeq && byKey && ! ses.cfgOpen;
        if (! shutByMaster) std::printf ("    (to the master %d, %d bus knobs, %d lane keys, shut by the strip %d, by its key %d, open %d)\n", (int) toMaster, busKnobs, laneKeys, (int) shutBySeq, (int) byKey, (int) ses.cfgOpen);
        ses.bringLane (4, 1);
        sc.keyPressed (juce::KeyPress ('c', juce::ModifierKeys(), 'c')); sc.testTick();
        click (bare ([] (const Screen::TestHit& h) { return Screen::testIsFront (h, S::fMaster, 0); })); sc.testTick();
        const bool mouseLeaves = ses.cfgOpen && ses.cfg.lane == 4 && ses.front.kind == S::fMaster;      // (under the mouse CONFIG stays where it was put)
        sc.keyPressed (juce::KeyPress ('c', juce::ModifierKeys(), 'c')); sc.testTick();        // (C: to what is in front now, the master's)
        sc.keyPressed (juce::KeyPress ('c', juce::ModifierKeys(), 'c')); sc.testTick();        // (and shut)
        check (opened && along && toInsert && shutByMaster && mouseLeaves && ! ses.cfgOpen,
               "C opens CONFIG on what is in front and shuts it; brought to the front by a key or a controller, a lane, an insert or the master (its drum bus: four knobs on its face, the eleven lanes among its CONFIG) takes CONFIG with it and the sequencer strip shuts it; the master's own C opens it; under the mouse it stays where it was put");

        ses.wantShowing = 1; sc.testTick();
        const bool toLibrary = sc.testPage() == Screen::pLibrary && ses.showing == 1 && ses.wantShowing == -1;
        ses.wantShowing = 0; sc.testTick();
        // the pointer: a frame round the module it is on, and nothing brought to the front for it
        ses.bringLane (5, 0); ses.point ({ S::fReturns, 0, 0 }); sc.testTick(); draw();
        const auto fr = sc.testStopRect ({ S::fReturns, 0, 0 });
        const auto with = sc.createComponentSnapshot (sc.getLocalBounds(), false, 1.0f);
        const bool untouched = ses.front.kind == S::fLane && ses.activeLane == 5;
        // on a lane's insert, the insert panels show that lane's (BASS has none; the kick's first is a DELAY)
        auto hasKnob = [&] (int ctl) { for (auto& h : sc.testHits()) if (Screen::testIsControl (h, ctl)) return true; return false; };
        const bool basses = ! hasKnob (cx::ins (0, 0, 1));
        ses.point ({ S::fInsert, 0, 0 }); sc.testTick(); draw();
        const bool kicks = hasKnob (cx::ins (0, 0, 1)) && ses.activeLane == 5 && ! sc.testStopRect ({ S::fInsert, 0, 0 }).isEmpty();
        ses.pointOff(); sc.testTick(); draw();
        const auto without = sc.createComponentSnapshot (sc.getLocalBounds(), false, 1.0f);
        const auto at = juce::Point<float> (fr.getX() + 2.0f, fr.getCentreY()).roundToInt();
        const auto cw = with.getPixelAt (at.x, at.y), co = without.getPixelAt (at.x, at.y);
        check (toLibrary && sc.testPage() == Screen::pMain && ! fr.isEmpty() && cw.getRed() > 180 && cw.getGreen() > 150 && cw.getBlue() < 140 && cw != co && untouched && basses && kicks && ! hasKnob (cx::ins (0, 0, 1)),
               ("a controller's wish for the library is taken up, and the way back; the pointer puts a yellow frame round the module it is on (" + cw.toDisplayString (false) + " where it was " + co.toDisplayString (false)
                    + ") and brings nothing to the front; on a lane's insert, the insert panels show that lane's").toRawUTF8());
        for (int l = 0; l < kLanes; ++l) for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (l, sl, insertsWere[(size_t) l][(size_t) sl]);
        ses.bringLane (7, 0); sc.testTick(); draw();
    }
    // the library: with a snapshot cued, the parts RECALL takes can be put out; after the recall all are lit again
    {
        ses.userSet (cx::kGlide, 0.0f);
        const int cut = cx::lane (5, 6), punch = cx::lane (0, 5);
        ses.userSet (cut, 0.15f); ses.userSet (punch, 0.15f);
        ses.take(); ses.finishTakes (true);
        ses.userSet (cut, 0.9f); ses.userSet (punch, 0.9f);
        sc.setPage (Screen::pLibrary); draw();
        auto firstOf = [&] (bool (*is) (const Screen::TestHit&), int nth = 0) { int n = 0; for (auto& h : sc.testHits()) if (is (h) && n++ == nth) return h.r.getCentre(); return juce::Point<float>(); };
        auto countOf = [&] (bool (*is) (const Screen::TestHit&)) { int n = 0; for (auto& h : sc.testHits()) if (is (h)) ++n; return n; };
        if (sc.testCueMode()) { click (firstOf (Screen::testIsCueKey)); draw(); }      // (left on by a check above: off, to begin)
        const bool noneYet = countOf (Screen::testIsScope) == 0;
        click (firstOf (Screen::testIsCueKey)); draw();
        int cards = countOf (Screen::testIsCard);
        click (firstOf (Screen::testIsCard, cards - 1)); draw();            // the one just taken: cued (or chosen, if it has no sound kept)
        const bool row = countOf (Screen::testIsScope) == kLanes + 3;
        click (firstOf (Screen::testIsScope, 0)); draw();                   // the kick put out
        const bool out = ! sc.testScope (0) && sc.testScope (5);
        click (firstOf (Screen::testIsRecallKey)); draw();
        const bool took = std::abs (e.target (cut) - 0.15f) < 1.0e-4f && std::abs (e.target (punch) - 0.9f) < 1.0e-4f && sc.testScope (0);
        if (! (noneYet && row && out && took)) std::printf ("    (none yet %d row %d out %d took %d: %d cards, %d keys, cut %.2f punch %.2f)\n", (int) noneYet, (int) row, (int) out, (int) took, cards, countOf (Screen::testIsScope), e.target (cut), e.target (punch));
        check (noneYet && row && out && took, "the library, a snapshot cued: fourteen keys say what RECALL takes (the lanes, the returns, the drum bus, the key); with the kick put out the recall leaves the kick as it is and takes the rest, and all are lit again after");
        if (sc.testCueMode()) { click (firstOf (Screen::testIsCueKey)); draw(); }      // (CUE off again)
        // NOTES: a key in the library and one on the snapshot module, and N on the keyboard
        {
            const bool was = ses.recallNotes;
            const int inLibrary = countOf (Screen::testIsNotesKey);
            click (firstOf (Screen::testIsNotesKey)); draw();
            const bool outByKey = ses.recallNotes != was;
            sc.setPage (Screen::pMain); draw();
            const int onMain = countOf (Screen::testIsNotesKey);
            click (firstOf (Screen::testIsNotesKey)); draw();
            const bool backByKey = ses.recallNotes == was;
            sc.keyPressed (juce::KeyPress ('n', juce::ModifierKeys(), 'n')); sc.testTick();
            const bool byN = ses.recallNotes != was;
            sc.keyPressed (juce::KeyPress ('n', juce::ModifierKeys(), 'n')); sc.testTick();
            check (was && inLibrary == 1 && onMain == 1 && outByKey && backByKey && byN && ses.recallNotes == was,
                   "NOTES (whether a recall brings a snapshot's notes) is lit to begin with; its key in the library, its key on the snapshot module and N on the keyboard each put it out and light it again");
        }
        sc.setPage (Screen::pMain); draw();
    }
    audio (200);
    draw();
    bool finite = true;
    for (int i = 0; i < 512; ++i) finite = finite && std::isfinite (b.getSample (0, i)) && std::isfinite (b.getSample (1, i));
    check (true, (juce::String (clicks) + " controls clicked, dragged and double-clicked across four screens").toRawUTF8());
    check (finite && ! e.fault.load(), "the audio is still clean afterwards");

    // POCKETS: the master's page. A key on the master and P on the keyboard; every lane's pocket on
    // a map, dragged by hand; the rig's four knobs and each lane's four; AUTO and RESET.
    {
        const juce::KeyPress pKey ('p', juce::ModifierKeys(), 'p');
        sc.setPage (Screen::pMain); ses.userSet (cx::kGlide, 0.0f); ses.pocketsReset();
        for (int k = 0; k < kPkGlobals; ++k) ses.userSet (cx::kPkG + k, e.control (cx::kPkG + k).def);
        for (int l = 0; l < kLanes; ++l) { ses.userSet (cx::lane (l, cx::kMute), 0.0f); ses.userSet (cx::lane (l, cx::kSolo), 0.0f); }
        audio (4); draw();
        int keys = 0; juce::Point<float> keyAt;
        for (auto& h : sc.testHits()) if (Screen::testIsPocketsKey (h)) { ++keys; keyAt = h.r.getCentre(); }
        const bool idle = ! e.pocketsForTests().working() && ! e.pocketsWatched();
        click (keyAt); draw();
        audio (4);
        const bool opened = sc.testPage() == Screen::pPockets && ses.showing == 3 && e.pocketsWatched() && e.pocketsForTests().working();
        int knobs = 0, rig = 0, maps = 0, mutes = 0;
        juce::Rectangle<float> bassMap, autoKey, resetKey, bassHome;
        for (auto& h : sc.testHits())
        {
            for (int l = 0; l < kLanes; ++l)
            {
                for (int k = 0; k < kPkLane; ++k) if (Screen::testIsControl (h, cx::pk (l, k))) { ++knobs; if (l == 5 && k == 0) bassHome = h.r; }
                if (Screen::testIsPocket (h, l)) { ++maps; if (l == 5) bassMap = h.r; }
                if (Screen::testIsToggle (h, cx::lane (l, cx::kMute)) || Screen::testIsToggle (h, cx::lane (l, cx::kSolo))) ++mutes;
            }
            for (int k = 0; k < kPkGlobals; ++k) if (Screen::testIsControl (h, cx::kPkG + k)) ++rig;
            if (Screen::testIsPocketsKey (h, 1)) autoKey = h.r;
            if (Screen::testIsPocketsKey (h, 2)) resetKey = h.r;
        }
        check (keys == 1 && idle && opened && knobs == kLanes * kPkLane && rig == kPkGlobals && maps == kLanes && mutes == 2 * kLanes,
               "POCKETS: a key on the master opens its page, and only then does it look at the lanes (AMOUNT being at nought): a map and four knobs for every lane, the rig's four, and each lane's MUTE and SOLO");
        // a pocket dragged along its map is its HOME, dragged up its WIDTH; double-clicked, both are back
        const int home = cx::pk (5, 0), width = cx::pk (5, 1);
        const float h0 = e.target (home), w0 = e.target (width);
        dragBy (bassMap.getCentre(), bassMap.getWidth() * 0.25f, 0.0f);
        const float h1 = e.target (home), w1 = e.target (width);
        dragBy (bassMap.getCentre(), 0.0f, -80.0f);
        const float h2 = e.target (home), w2 = e.target (width);
        sc.mouseDoubleClick (ev (bassMap.getCentre(), bassMap.getCentre(), false));
        const bool dragged = std::abs (h1 - (h0 + 0.25f * 28.0f / 27.0f)) < 0.01f && w1 == w0 && h2 == h1 && std::abs (w2 - (w0 + 0.5f)) < 0.01f
                          && std::abs (e.target (home) - h0) < 1.0e-6f && std::abs (e.target (width) - w0) < 1.0e-6f;
        // its knob, as any knob
        dragBy (bassHome.getCentre(), 0.0f, -32.0f);
        const bool knobbed = std::abs (e.target (home) - (h0 + 0.2f)) < 0.01f;
        check (dragged && knobbed, "a pocket dragged a quarter of the way along its map moves its HOME by that much, dragged up it widens, and a double-click puts both back; its HOME knob turns it too");
        // AUTO places the pockets from what has been playing while the page was up; RESET puts them back
        audio (700); sc.testTick(); draw();
        click (autoKey.getCentre()); draw();
        audio (4);
        // (the rig is as the clicking above left it: what matters here is that the key places them, by what is playing. --test holds AUTO to where.)
        int movedLanes = 0;
        for (int l = 0; l < kLanes; ++l) if (std::abs (e.target (cx::pk (l, 0)) - e.control (cx::pk (l, 0)).def) > 1.0e-4f || std::abs (e.target (cx::pk (l, 1)) - e.control (cx::pk (l, 1)).def) > 1.0e-4f) ++movedLanes;
        const float hat = Pockets::homeHz (e.target (cx::pk (1, 0)));
        const bool placed = movedLanes >= 3 && hat > 3000.0f && std::abs (e.target (cx::pk (5, 2)) - e.control (cx::pk (5, 2)).def) < 1.0e-6f;
        click (resetKey.getCentre()); draw(); audio (4);
        bool reset = true;
        for (int l = 0; l < kLanes; ++l) for (int k = 0; k < kPkLane; ++k) reset = reset && std::abs (e.target (cx::pk (l, k)) - e.control (cx::pk (l, k)).def) < 1.0e-6f;
        check (placed && reset, ("AUTO moves the pockets to where the lanes have been playing (" + juce::String (movedLanes) + " of them moved; the closed hat's is at " + juce::String (hat / 1000.0f, 1) + " kHz) and leaves HOLD alone; RESET puts every pocket back where the rig starts it").toRawUTF8());
        // INFO: every knob, key and pocket on the page says what it is, and where MIDI finds it
        sc.setInfo (true); draw();
        int asked = 0, silent = 0;
        for (auto& h : sc.testHits())
        {
            if (! Screen::testAnswers (h)) continue;
            sc.testPointAt (h.r.getCentre());
            ++asked;
            if (sc.testTip().trim().isEmpty()) ++silent;
        }
        sc.testPointAt (bassHome.getCentre());
        const auto homeTip = sc.testTip();
        for (auto& h : sc.testHits()) if (Screen::testIsControl (h, cx::kPkG)) sc.testPointAt (h.r.getCentre());
        const auto amountTip = sc.testTip();
        sc.setInfo (false);
        check (asked >= kLanes * (kPkLane + 3) + kPkGlobals + 3 && silent == 0 && homeTip.contains ("6 BASS") && homeTip.contains ("CHANNEL 6, CC 59") && amountTip.contains ("CHANNEL 16, CC 75"),
               ("with INFO on, each of the page's " + juce::String (asked) + " knobs, keys and pockets says what it is; the bass's HOME is on channel 6, CC 59, and AMOUNT on channel 16, CC 75").toRawUTF8());
        // Esc and P; and a controller asking for the page
        sc.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); draw(); audio (4);
        const bool left = sc.testPage() == Screen::pMain && ! e.pocketsWatched() && ! e.pocketsForTests().working();
        sc.keyPressed (pKey); draw();
        const bool byP = sc.testPage() == Screen::pPockets;
        sc.keyPressed (pKey); draw();
        const bool backByP = sc.testPage() == Screen::pMain;
        ses.wantShowing = 3; sc.testTick(); draw();
        const bool asked3 = sc.testPage() == Screen::pPockets && ses.pocketsUp() && (int) ses.configList().size() == Session::kPocketList;
        ses.wantShowing = 0; sc.testTick(); draw();
        check (left && byP && backByP && asked3 && sc.testPage() == Screen::pMain && ! e.pocketsWatched(),
               "Esc leaves the page, and it stops looking; P on the keyboard opens and shuts it; a controller that asks for it gets it, with its forty-eight knobs listed for the CONFIG pages");
        // AMOUNT turned up is heard, and shows on the master
        ses.userSet (cx::kPkG, 1.0f); audio (300);
        float cut = 0.0f; for (int q = 0; q < Pockets::kParts; ++q) for (int k = 0; k < Pockets::kBands; ++k) cut = std::max (cut, e.pocketsForTests().cutNow (q, k));
        ses.userSet (cx::kPkG, 0.0f); audio (300);
        check (cut > 2.0f && ! e.pocketsForTests().working(), ("with AMOUNT at full the playing rig is carved (up to " + juce::String (cut, 1) + " dB in a band); back at nought, once the last bell has let go, POCKETS is doing nothing again").toRawUTF8());
    }

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
    // 3b. LEARN: a snapshot knob tied straight to knobs
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_learn_test");
        tmp.deleteRecursively(); tmp.createDirectory();
        const auto was = Session::stateFileForTests;
        Session::stateFileForTests = tmp.getChildFile ("session.json");
        Engine e2; e2.prepare (48000.0, 512);
        {
            Session s2 (e2, true);
            const int cut = e2.find ("bass.cutoff"), res = e2.find ("bass.res"), dec = e2.find ("closed_hat.decay"), tune = e2.find ("kick.tune"), sendA = cx::lane (7, cx::kSendA);
            auto at = [&] (int i) { return e2.target (i); };
            s2.userSet (cx::kGlide, 0.0f);
            s2.userSet (cut, 0.30f); s2.userSet (res, 0.20f); s2.userSet (dec, 0.40f); s2.userSet (sendA, 0.10f);
            const float tune0 = at (tune);
            const bool deadBefore = ! s2.knobLive (8) && s2.knobScope (8).empty();
            // taught: three knobs moved, one of them down
            s2.learnBegin (8);
            const bool begun = s2.learning == 8 && at (cx::kSnap + 8) == 1.0f && s2.knobLive (8) && s2.learnMoves() == 0;
            s2.userSet (cut, 0.80f); s2.userSet (dec, 0.25f); s2.userSet (sendA, 0.50f);
            s2.userSet (res, 0.60f); s2.userSet (res, 0.20f);                     // (turned, and turned back: not one of them)
            s2.turnSnapKnob (8, 0.3f);                                             // (it stands at full while it is taught)
            const bool counting = s2.learnMoves() == 3 && s2.learnMoved (cut) && ! s2.learnMoved (res) && at (cx::kSnap + 8) == 1.0f;
            s2.learnKeep();
            const auto& kn = s2.knobs[8];
            const bool kept = s2.learning < 0 && kn.links.size() == 3 && s2.knobScope (8).size() == 3
                           && std::abs (at (cut) - 0.80f) < 1e-4f && std::abs (at (dec) - 0.25f) < 1e-4f && std::abs (at (sendA) - 0.50f) < 1e-4f && at (cx::kSnap + 8) == 1.0f;
            check (deadBefore && begun && counting && kept,
                   "LEARN: the knob goes to its full turn; three knobs moved (one turned and turned back is not one of them) are tied to it by KEEP, and nothing changes in the sound");
            s2.turnSnapKnob (8, 0.5f);
            const bool half = std::abs (at (cut) - 0.55f) < 1e-4f && std::abs (at (dec) - 0.325f) < 1e-4f && std::abs (at (sendA) - 0.30f) < 1e-4f;
            s2.turnSnapKnob (8, 0.0f);
            const bool home = std::abs (at (cut) - 0.30f) < 1e-4f && std::abs (at (dec) - 0.40f) < 1e-4f && std::abs (at (sendA) - 0.10f) < 1e-4f && std::abs (at (res) - 0.20f) < 1e-4f && at (tune) == tune0;
            // from wherever the knobs stand, and stacked on a snapshot knob that moves the same one
            s2.userSet (cut, 0.10f);
            s2.turnSnapKnob (8, 1.0f);
            const bool fromThere = std::abs (at (cut) - 0.60f) < 1e-4f;
            const int dens = e2.find ("closed_hat.density");
            const float d0 = at (dens);
            s2.turnSnapKnob (0, 1.0f);                                             // ENERGY, a snapshot knob
            const bool stacked = at (dens) > d0 + 0.2f && std::abs (at (cut) - 0.60f) < 0.25f;
            s2.turnSnapKnob (0, 0.0f); s2.turnSnapKnob (8, 0.0f);
            check (half && home && fromThere && stacked, "turned, each tied knob goes its own share of its own way (BASS CUTOFF up, CLOSED HAT DECAY down) from wherever it stands, nothing else moves, and it stacks with a snapshot knob");
            // LEARN again adds to what it has; a tied knob turned back to where zero has it is let go; DROP forgets
            s2.userSet (cut, 0.30f);
            s2.learnBegin (8);
            s2.userSet (res, 0.45f);                                               // one more
            s2.userSet (sendA, 0.10f);                                             // (at full it stood at 0.50: back to where the knob's zero has it)
            s2.learnKeep();
            const bool added = kn.links.size() == 3 && s2.knobScope (8) == std::vector<int> ({ dec, std::min (cut, res), std::max (cut, res) });
            s2.turnSnapKnob (8, 0.0f);
            const bool addedHome = std::abs (at (res) - 0.20f) < 1e-4f && std::abs (at (sendA) - 0.10f) < 1e-4f;
            s2.learnBegin (8);
            s2.userSet (tune, 0.9f);
            s2.learnDrop();
            const bool dropped = s2.learning < 0 && kn.links.size() == 3 && at (tune) == tune0 && at (cx::kSnap + 8) == 0.0f;
            check (added && addedHome && dropped, "LEARN again adds a knob to what it has and lets go of one turned back to where it was; DROP puts back what was moved and ties nothing");
            // a press with no snapshot to land on: past half way what it has pushed is kept, and the knob goes home
            s2.turnSnapKnob (8, 0.7f);
            s2.pressSnapKnob (8);
            const bool landed = at (cx::kSnap + 8) == 0.0f && std::abs (at (cut) - 0.80f) < 1e-4f && std::abs (at (dec) - 0.25f) < 1e-4f;
            s2.turnSnapKnob (8, 0.3f);
            s2.pressSnapKnob (8);
            const bool backAgain = at (cx::kSnap + 8) == 0.0f && std::abs (at (cut) - 0.80f) < 1e-4f;
            // a recall while a knob is being taught: what it is shown is measured from the recalled scene
            s2.learnBegin (9);
            s2.recall (1000);
            const float cutInit = at (cut);
            s2.userSet (cut, juce::jlimit (0.0f, 1.0f, cutInit + 0.2f));
            s2.learnKeep();
            const bool afterRecall = s2.knobs[9].links.size() == 1 && std::abs (s2.knobs[9].links[0].amount - (at (cut) - cutInit)) < 1e-4f;
            check (landed && backAgain && afterRecall, "its press, with no snapshot to land on: past half way what it has pushed stays and the knob goes home, short of it they go back; a recall while one is taught starts the measuring again");
            s2.knobs[8].name = "OPEN UP";
            s2.touch(); s2.save();
        }
        {
            // the session file: the links by each knob's id, read back; one to a knob this build does not have is carried
            auto text = Session::stateFileForTests.loadFileAsString();
            const bool written = text.contains ("\"links\"") && text.contains ("bass.cutoff") && text.contains ("closed_hat.decay");
            auto root = juce::JSON::parse (text);
            if (auto* kn = root.getProperty ("knobs", juce::var()).getArray())
                if (auto* ls = (*kn)[8].getProperty ("links", juce::var()).getArray())
                {
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("c", "theremin.wobble"); o->setProperty ("d", 0.25);
                    ls->add (juce::var (o));
                }
            Session::stateFileForTests.replaceWithText (juce::JSON::toString (root));
            Engine e3; e3.prepare (48000.0, 512);
            Session s3 (e3, true);
            s3.load();
            const int cut = e3.find ("bass.cutoff");
            const bool read = s3.knobs[8].name == "OPEN UP" && s3.knobs[8].links.size() == 3 && s3.knobLive (8) && s3.knobs[9].links.size() == 1;
            const float c0 = e3.target (cut);
            s3.userSet (cx::kGlide, 0.0f);
            s3.turnSnapKnob (8, 1.0f);
            const bool works = std::abs (e3.target (cut) - juce::jlimit (0.0f, 1.0f, c0 + 0.5f)) < 1e-3f;
            s3.touch(); s3.save();
            const bool carried = Session::stateFileForTests.loadFileAsString().contains ("theremin.wobble");
            check (written && read && works && carried, "the session file holds what a knob is tied to, by each knob's own id; read back it does the same, and a tie to a knob this build does not have is carried through a save");
        }
        Session::stateFileForTests = was;
        tmp.deleteRecursively();
    }
    // 3c. LEARN on the screens: the key in the macro designer, KEEP and DROP in the top bar
    {
        auto actKey = [&] (bool keep) { for (auto& h : sc.testHits()) if (Screen::testIsLearnKey (h, keep ? 1 : 2)) return h.r.getCentre(); return juce::Point<float> (-1.0f, -1.0f); };
        auto learnKey = [&] { for (auto& h : sc.testHits()) if (Screen::testIsLearnKey (h, 0)) return h.r.getCentre(); return juce::Point<float> (-1.0f, -1.0f); };
        sc.setPage (Screen::pMain); draw();
        const bool noneYet = actKey (true).x < 0.0f && actKey (false).x < 0.0f;
        sc.setPage (Screen::pMacros); sc.testPickKnob (10); draw();
        click (learnKey()); draw();
        const bool toMain = ses.learning == 10 && sc.testPage() == Screen::pMain && actKey (true).x > 0.0f && actKey (false).x > 0.0f;
        const int filt = cx::lane (3, 10);
        const float f0 = ses.value (filt);
        ses.userSet (filt, f0 > 0.5f ? f0 - 0.3f : f0 + 0.3f); draw();
        click (actKey (false)); draw();
        const bool droppedIt = ses.learning < 0 && std::abs (ses.value (filt) - f0) < 1e-4f && ses.knobs[10].links.empty() && actKey (true).x < 0.0f;
        sc.setPage (Screen::pMacros); sc.testPickKnob (10); draw();
        click (learnKey()); draw();
        ses.userSet (filt, f0 > 0.5f ? f0 - 0.3f : f0 + 0.3f); draw();
        click (actKey (true)); draw();
        const bool keptIt = ses.learning < 0 && ses.knobs[10].links.size() == 1 && ses.knobs[10].links[0].ctl == filt && actKey (true).x < 0.0f;
        bool shown = false;
        for (auto& h : sc.testHits()) shown = shown || Screen::testIsSnapKnob (h, 10);      // (it is a knob on the main screen now)
        ses.turnSnapKnob (10, 0.0f);
        ses.knobs[10].links.clear();
        check (noneYet && toMain && droppedIt && keptIt && shown && std::abs (ses.value (filt) - f0) < 1e-4f,
               "LEARN in the macro designer brings the main screen up with KEEP and DROP in its top bar; DROP puts the knob back and ties nothing; KEEP ties it, and the snapshot knob is there to turn");
        sc.setPage (Screen::pMain); draw();
    }
    // 3d. MOTION in the macro designer: three rows of four knobs, turned like any other
    {
        auto at = [&] (int ctl) { for (auto& h : sc.testHits()) if (Screen::testIsControl (h, ctl)) return h.r.getCentre(); return juce::Point<float> (-1.0f, -1.0f); };
        sc.setPage (Screen::pMain); draw();
        const bool notOnMain = at (cx::mot (0, 0)).x < 0.0f;
        sc.setPage (Screen::pMacros); draw();
        int there = 0;
        for (int i = cx::kMot; i < cx::kSnap; ++i) there += at (i).x >= 0.0f ? 1 : 0;
        ses.knobs[9].links = { { cx::lane (3, 10), 0.3f } };
        ses.userSet (cx::mot (1, 2), 9.0f / 15.0f);
        const auto wave = at (cx::mot (1, 0)), amount = at (cx::mot (1, 3));
        dragBy (wave, 0.0f, -30.0f); draw();
        const bool waveOn = ses.engine.valueText (cx::mot (1, 0)) != "OFF" && ses.motionOn (9) == 0;      // (a wave and no amount: not on yet)
        dragBy (amount, 0.0f, -60.0f); draw();
        const bool on = ses.motionOn (9) == 2 && ses.engine.target (cx::mot (1, 3)) > 0.55f;
        juce::String where, label; const char* what = nullptr;
        const bool said = sc.testDescribe (cx::mot (1, 3), where, label, what) && where == "SNAPSHOT KNOBS, MOTION 2" && label == "AMOUNT" && what != nullptr && juce::String (what).contains ("WHOLE TURN")
                       && sc.testDescribe (cx::mot (2, 0), where, label, what) && what != nullptr && sc.testDescribe (cx::mot (0, 1), where, label, what) && what != nullptr && sc.testDescribe (cx::mot (0, 2), where, label, what) && what != nullptr;
        sc.mouseDoubleClick (ev (amount, amount, false)); sc.mouseDoubleClick (ev (wave, wave, false)); draw();
        const bool back = ses.motionOn (9) == 0 && ses.engine.valueText (cx::mot (1, 0)) == "OFF" && ses.engine.valueText (cx::mot (1, 3)) == "NONE";
        ses.knobs[9].links.clear(); ses.userSet (cx::mot (1, 2), 0.0f);
        check (notOnMain && there == cx::kMotUnits * cx::kMotKnobs && waveOn && on && said && back,
               ("MOTION is in the macro designer: three rows of WAVE, SPEED, KNOB, AMOUNT (" + juce::String (there) + " knobs), dragged like any knob (a wave with no amount is not on; with one it is, and the knob's card says so), each with its help, and a double-click puts one back").toRawUTF8());
        sc.setPage (Screen::pMain); draw();
    }
    // 3e. the arrangement's page: parts added from the library, their bars and morph dragged, moved, played, printed
    {
        auto key = [&] (int which) { for (auto& h : sc.testHits()) if (Screen::testIsArrKey (h, which)) return h.r.getCentre(); return juce::Point<float> (-1.0f, -1.0f); };
        auto part = [&] (int i, int what) { for (auto& h : sc.testHits()) if (Screen::testIsArrPart (h, i, what)) return h.r.getCentre(); return juce::Point<float> (-1.0f, -1.0f); };
        auto pick = [&] (int i) { for (auto& h : sc.testHits()) if (Screen::testIsPick (h, i)) return h.r.getCentre(); return juce::Point<float> (-1.0f, -1.0f); };
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sf000_arrange_ui");
        tmp.deleteRecursively(); tmp.createDirectory();
        Recorder rec (ses.engine);
        rec.root = tmp;
        sc.recorder = &rec;
        while (! ses.arrangement.empty()) ses.arrRemove (0);
        ses.engine.setRunning (false); audio (4);
        sc.setPage (Screen::pMain); draw();
        const bool onMain = key (0).x >= 0.0f && key (1).x < 0.0f;
        click (key (0)); draw();
        const bool opened = sc.testPage() == Screen::pArrange && key (1).x < 0.0f && key (7).x < 0.0f;       // (nothing in it: nothing to play or print)
        const auto first = ses.library[1].id, second = ses.library[2].id, third = ses.library[3].id;
        click (pick (1)); draw(); click (pick (2)); draw();
        const bool added = ses.arrangement.size() == 2 && ses.arrangement[0].snap == first && ses.arrangement[1].snap == second && sc.testSelPart() == 1;
        click (part (0, 0)); draw();
        click (pick (3)); draw();                                                   // (after the one chosen: between the two)
        const bool between = ses.arrangement.size() == 3 && ses.arrangement[1].snap == third && sc.testSelPart() == 1;
        dragBy (part (1, 1), 0.0f, -40.0f); draw();
        const int bars = ses.arrangement[1].bars;
        dragBy (part (1, 2), 0.0f, -70.0f); draw();
        const int morph = ses.arrangement[1].morph;
        const bool firstHasNoMorph = part (0, 2).x < 0.0f;
        { const auto p = part (1, 1); sc.mouseDoubleClick (ev (p, p, false)); } draw();
        const bool dragged = bars > 8 && bars <= 16 && morph >= 2 && ses.arrangement[1].bars == 8 && firstHasNoMorph;
        click (key (4)); draw();                                                    // LATER
        const bool later = ses.arrangement[2].snap == third && sc.testSelPart() == 2;
        click (key (3)); draw();                                                    // EARLIER
        const bool earlier = ses.arrangement[1].snap == third && sc.testSelPart() == 1;
        click (key (5)); draw();                                                    // REMOVE
        const bool removed = ses.arrangement.size() == 2 && ses.arrangement[1].snap == second;
        ses.arrSet (0, 1, 0); ses.arrSet (1, 1, 2);
        click (key (1)); draw(); audio (8); ses.arrTick();
        const bool plays = ses.arrPlaying() && ses.engine.isRunning() && ses.baseId == first;
        click (key (1)); draw(); audio (8);
        const bool stops = ! ses.arrPlaying() && ! ses.engine.isRunning();
        // PRINT: the page renders it a slice at a time; nothing else is taken meanwhile; Esc stops it
        click (key (7)); draw();
        const bool printing = ses.printing();
        click (key (0)); click (pick (4)); draw();                                  // (ignored)
        const bool locked = sc.testPage() == Screen::pArrange && ses.arrangement.size() == 2 && ses.printing();
        sc.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); draw();
        const bool escaped = ! ses.printing() && ses.printNote == "PRINT STOPPED" && tmp.getNumberOfChildFiles (juce::File::findDirectories) == 0;
        click (key (7)); draw();
        int ticks = 0;
        while (ses.printing() && ticks < 4000) { sc.testTick(); ++ticks; }
        const bool printed = ! ses.printing() && ses.printNote.startsWith ("PRINTED") && ses.printFolder.getChildFile ("00 SET.wav").existsAsFile() && ses.printFolder.getParentDirectory() == tmp && ticks > 2;
        draw();
        // HOLD: CLEAR
        { const auto p = key (6); sc.mouseDown (ev (p, p, false)); juce::Thread::sleep (760); sc.testTick(); sc.mouseUp (ev (p, p, false)); } draw();
        const bool cleared = ses.arrangement.empty() && sc.testSelPart() == -1;
        sc.keyPressed (juce::KeyPress ('a', juce::ModifierKeys(), 'a')); draw();
        const bool shut = sc.testPage() == Screen::pMain;
        check (onMain && opened && added && between && dragged && later && earlier && removed && plays && stops && printing && locked && escaped && printed && cleared && shut,
               ("the ARRANGE key opens the arrangement's page; a snapshot tapped there becomes a part after the one chosen; a part's BARS and MORPH are dragged (" + juce::String (bars) + " bars, " + Session::morphName (morph)
                    + "; a double-click: eight again); EARLIER, LATER and REMOVE; PLAY and STOP; PRINT is rendered by the page a slice at a time (" + juce::String (ticks) + " of them), takes nothing else meanwhile and stops for Esc; CLEAR, held, empties it; A shuts it").toRawUTF8());
        if (! (onMain && opened && added && between && dragged && later && earlier && removed && plays && stops && printing && locked && escaped && printed && cleared && shut))
            std::printf ("        (main %d opened %d added %d between %d dragged %d [%d bars, morph %d, first %d] later %d earlier %d removed %d plays %d stops %d printing %d locked %d escaped %d printed %d [%s] cleared %d shut %d)\n",
                         onMain, opened, added, between, dragged, bars, morph, firstHasNoMorph, later, earlier, removed, plays, stops, printing, locked, escaped, printed, ses.printNote.toRawUTF8(), cleared, shut);
        sc.recorder = nullptr;
        tmp.deleteRecursively();
        sc.setPage (Screen::pMain); draw();
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
    sc.setPage (page == "library" ? Screen::pLibrary : page == "macros" ? Screen::pMacros : page == "pockets" ? Screen::pPockets : page == "arrange" ? Screen::pArrange : Screen::pMain);
    if (page == "arrange")
    {
        // an arrangement made of the snapshots it comes with, caught playing a little way into its third part
        static const int bars[] = { 8, 16, 8, 32, 16, 8, 16 }, morph[] = { 0, 5, 0, 7, 4, 0, 6 };
        for (int i = 0; i < 7 && i + 1 < (int) ses.library.size(); ++i) { ses.arrAdd (ses.library[(size_t) (1 + (i * 3) % ((int) ses.library.size() - 1))].id); ses.arrSet (i, bars[i], morph[i]); }
        ses.arrPlay (2);
        const int blocks = (int) ((14.0 * 60.0 / e.tempo()) * 48000.0 / 512.0);
        for (int i = 0; i < blocks; ++i) { e.render (b); ses.arrTick(); }
        sc.testPickPart (3);
        sc.testTick();
    }
    if (page == "pockets")
    {
        // POCKETS at work: AMOUNT up (--amount, in hundredths: 60 unless said), a bar and a half played with the page up, caught on a kick
        ses.userSet (cx::kPkG, (float) arg ("--amount", "60").getIntValue() / 100.0f);
        const int blocks = (int) ((6.0 * 60.0 / e.tempo() + 0.03) * 48000.0 / 512.0);
        for (int i = 0; i < blocks; ++i) e.render (b);
        sc.testTick();
    }
    if (args.contains ("--motion"))
    {
        // MOTION at work on two snapshot knobs: a sine a bar long on the first, the kick on the fourth; caught a little way into a bar
        const auto waves = Engine::motionWaves();
        auto wave = [&] (const char* n) { return (float) waves.indexOf (n) / (float) (waves.size() - 1); };
        ses.userSet (cx::kSnap, 0.4f);
        ses.userSet (cx::mot (0, 0), wave ("SINE")); ses.userSet (cx::mot (0, 3), 0.7f);
        ses.userSet (cx::mot (1, 0), wave ("KICK")); ses.userSet (cx::mot (1, 1), 2.0f / 20.0f); ses.userSet (cx::mot (1, 2), 3.0f / 15.0f); ses.userSet (cx::mot (1, 3), 0.85f);
        const int blocks = (int) ((4.6 * 60.0 / e.tempo()) * 48000.0 / 512.0);
        for (int i = 0; i < blocks; ++i) e.render (b);
        sc.testTick();
    }
    const auto cfg = arg ("--cfg", "");
    if (cfg == "lane") sc.showConfig ({ 0, ses.activeLane, 0 });
    else if (cfg == "insert") sc.showConfig ({ 1, ses.activeLane, 0 });
    else if (cfg == "return") sc.showConfig ({ 2, 0, 0 });
    else if (cfg == "space") sc.showConfig ({ 2, 0, 1 });
    else if (cfg == "master") sc.showConfig ({ 3, 0, 0 });
    else if (cfg == "bands")
    {
        // a convolution in the active lane's first slot, with BANDS lit: the low and the top from place 2
        while (e.insertType (ses.activeLane, 0) != 2) ses.stepInsert (ses.activeLane, 0);
        const Engine::Target t { 1, ses.activeLane, 0 };
        e.setSetup (t, "bands", 1.0f); e.setSetup (t, "b1_place", 2.0f / 8.0f); e.setSetup (t, "b3_place", 2.0f / 8.0f); e.setSetup (t, "b1_decay", 0.3f); e.setSetup (t, "b3_size", 0.8f);
        for (int i = 0; i < 60; ++i) { e.render (b); juce::Thread::sleep (5); }
        sc.showConfig (t);
    }
    if (cfg.isNotEmpty() && args.contains ("--load")) sc.showLoad (arg ("--load", ""));         // LOAD's list, open on that folder
    if (args.contains ("--cue")) sc.showCue (arg ("--cue", "-1").getIntValue());                 // the library's CUE lit and a snapshot chosen (a number: that part put out of the recall)
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
    {
        // that module in front, and the wheel's pointer on that one (each a place among the stops: a lane, its inserts, the next lane ...)
        const auto all = ses.stops();
        auto stop = [&] (const char* name) { return all[(size_t) juce::jlimit (0, (int) all.size() - 1, arg (name, "0").getIntValue())]; };
        if (args.contains ("--front")) ses.bringStop (stop ("--front"), 1);
        if (args.contains ("--pointer")) ses.point (stop ("--pointer"));
    }
    if (args.contains ("--learn") || args.contains ("--learnt"))
    {
        // --learn N: snapshot knob N being taught, with a few knobs moved. --learnt N: after KEEP, the knob turned half way back.
        const int k = juce::jlimit (0, 15, arg (args.contains ("--learn") ? "--learn" : "--learnt", "6").getIntValue() - 1);
        ses.knobs[(size_t) k].name = "OPEN UP";
        ses.learnBegin (k);
        ses.userSet (cx::lane (1, 8), 0.82f); ses.userSet (cx::lane (2, 8), 0.74f);        // the hats' decay
        ses.userSet (cx::lane (5, 6), 0.9f); ses.userSet (cx::lane (5, 2), 0.55f);         // the bass's cutoff and resonance
        ses.userSet (cx::lane (7, cx::kSendA), 0.6f);                                       // the lead's send to the dub
        if (args.contains ("--learnt")) { ses.learnKeep(); ses.turnSnapKnob (k, 0.5f); }
        for (int i = 0; i < 40; ++i) e.render (b);
    }
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
    // the second scene beside it
    const auto lib = sf0::e16::fxText (e, snapKnobNames());
    const auto lf = f.getSiblingFile (f.getFileNameWithoutExtension() + "-FX" + f.getFileExtension());
    const bool ok2 = lf.replaceWithData (lib.toRawUTF8(), lib.getNumBytesAsUTF8());
    std::printf (ok2 ? "wrote %s (%d bytes): the second scene (the library; the inserts, the returns, the master, the sequencer strip, CONFIG, the snapshot knobs)\n" : "could not write %s\n", lf.getFullPathName().toRawUTF8(), (int) lib.getNumBytesAsUTF8());
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
        int pcs = 0, banks = 0;
        for (auto& m : out) { if (m.isProgramChange() && m.getChannel() == 12) ++pcs; if (m.isController() && m.getChannel() == 12) ++banks; }
        check ((int) told.size() == all + 2 && (int) out.size() == all + 3 && pcs == 1 && banks == 2 && firstGo <= 24 + e16::kLibKnobs + 1 + 3,
               "a fresh E16 is turned to the page of what is in front and told every value once (" + juce::String ((int) out.size()) + " messages: the scene and the page, " + juce::String (e16::kSlots) + " knobs, GLIDE and the library scene's "
               + juce::String (e16::kLibKnobs) + " rings), two dozen knobs at a time (" + juce::String (firstGo) + " in the first go)");
        out.clear();
        midi.incoming (cc (16, 119, ses.activeLane + 1), true);         // (its script says it is where it was sent)
        for (int k = 0; k < 4; ++k) midi.service();
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
                const bool gen = ctl < cx::kRet && ctl % cx::kPerLane >= cx::kGen && ctl % cx::kPerLane < cx::kGen + 4;      // (a generator's knob: the active lane's, on channel 13)
                right = right && (gen ? ch == e16::kFxChannel && num == e16::kGenCc + (ctl % cx::kPerLane - cx::kGen)
                                : slot >= 0 ? e16::controlFor (slot) == ctl
                                : extra >= 0 ? (ccx::controlFor (extra, 0) == ctl || ccx::controlFor (extra, 1) == ctl)
                                : (ctl == cx::kGlide && ch == 16 && num == e16::kGlideCc));
            }
            const int expected = kLanes * (kLaneKnobs + 5 + kSlots * kCells + 4 + 3 + kPkLane) + 4 + 4 * kSpaceModels + 3 + kBusKnobs + kPkGlobals + 1 + cx::kMotUnits * cx::kMotKnobs + 16;
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

            // MOTION on the snapshot knobs: three of four, on channel 16 from CC 102
            m3.incoming (cc (16, 102, 13), false);                      // MOTION 1's WAVE: SINE
            m3.incoming (cc (16, 108, 64), false);                      // MOTION 2's KNOB: the ninth
            m3.incoming (cc (16, 113, 96), false);                      // MOTION 3's AMOUNT: half way up from none
            m3.service();
            check (e3.valueText (cx::mot (0, 0)) == "SINE" && e3.valueText (cx::mot (1, 2)) == "9" && e3.valueText (cx::mot (2, 3)) == "+51" && back.empty(),
                   "a sequencer reaches MOTION on the snapshot knobs (CC 102-113 on channel 16: a list by its share of the number, AMOUNT with none at 64)");
            for (int i = cx::kMot; i < cx::kSnap; ++i) s3.userSet (i, e3.control (i).def);

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

    // 4c. what is in front, on the E16: its page turned from here, the second scene's pages, the
    // pointer's wheel, OPEN
    {
        Engine e; e.prepare (48000.0, 512);
        Session ses (e, false);
        Midi midi (e, ses, false);
        std::vector<juce::MidiMessage> out;
        midi.toE16 = [&] (const juce::MidiMessage& m) { out.push_back (m); };
        auto cc = [] (int ch, int num, int v) { return juce::MidiMessage::controllerEvent (ch, num, v); };
        // A made-up E16 on the other end, as far as pages go: sent a Bank Select it is in that scene, sent a Program Change it
        // is on that page, and its script says so (CC 119 on channel 16), which SF-000 hears the next time round.
        int mockScene = -1;
        bool mockAnswers = true;
        auto ticks = [&] (int n)
        {
            for (int k = 0; k < n; ++k)
            {
                size_t seen = out.size();
                midi.service();
                int page = 0;
                for (; seen < out.size(); ++seen)
                {
                    const auto& m = out[seen];
                    if (midi.pcChannel() <= 0 || m.getChannel() != midi.pcChannel()) continue;
                    if (m.isController() && m.getControllerNumber() == 32) mockScene = m.getControllerValue() == midi.sceneSlot (1) - 1 ? 1 : m.getControllerValue() == midi.sceneSlot (0) - 1 ? 0 : -1;
                    if (m.isProgramChange()) page = m.getProgramChangeNumber() + 1;
                }
                if (page > 0 && mockAnswers && mockScene >= 0) midi.incoming (cc (16, e16::kFocusCc, (mockScene == 1 ? e16::kFocusB : 0) + page), true);
            }
        };
        auto in = [&] (int ch, int num, int v) { midi.incoming (cc (ch, num, v), true); };
        auto press = [&] (int ch, int num) { in (ch, num, 127); in (ch, num, 0); };
        // the E16 turning its own page (a third of a second after anything sent to it: sooner than that, a page it says is taken for its answer to being sent)
        auto turnsTo = [&] (int says) { juce::Thread::sleep (310); in (16, e16::kFocusCc, says); };
        // the E16's script saying the page that is up, as it does when it has been sent to one
        auto answers = [&] (int says) { in (16, e16::kFocusCc, says); ticks (1); };
        // the page turns sent: Bank Select's two numbers and the Program Change, as "B0=0 B32=1 P3"
        auto turns = [&]
        {
            juce::String t;
            for (auto& m : out)
            {
                if (m.isController() && m.getChannel() == e16::kPcChannel) t << "B" << m.getControllerNumber() << "=" << m.getControllerValue() << " ";
                if (m.isProgramChange()) t << (m.getChannel() == e16::kPcChannel ? "P" : "?") << m.getProgramChangeNumber() << " ";
            }
            return t.trim();
        };
        auto ccOut = [&] (int ch, int num) { int v = -1; for (auto& m : out) if (m.isController() && m.getChannel() == ch && m.getControllerNumber() == num) v = m.getControllerValue(); return v; };
        auto said = [&] (int kind) { juce::String t; bool any = false; for (auto& m : out) if (m.isSysEx() && m.getSysExDataSize() >= 4 && m.getSysExData()[3] == kind) { any = true; t = juce::String::fromUTF8 ((const char*) m.getSysExData() + 4, m.getSysExDataSize() - 4); } return any ? t : juce::String ("(none)"); };
        auto name = [&] (int knob) { const auto t = said (4); return t.length() >= 65 ? t.substring (1 + (knob - 1) * 4, 5 + (knob - 1) * 4).trim() : juce::String ("?"); };

        {
            // the new addresses are no one else's
            std::set<int> seen;
            bool clean = true;
            for (int slot = 0; slot < e16::kSlots; ++slot) { int ch = 0, num = 0; e16::addressOf (slot, ch, num); seen.insert (ch * 128 + num); }
            for (int ctl = 0; ctl < cx::kCount; ++ctl) { int ch = 0, num = 0; if (ccx::addressOf (ctl, ch, num)) seen.insert (ch * 128 + num); }
            for (int k = 0; k < 16; ++k) { seen.insert (16 * 128 + e16::kPressCc + k); seen.insert (16 * 128 + ccx::kRecall + k); }
            seen.insert (16 * 128 + e16::kFocusCc);
            for (int num : { e16::kWheelCc, e16::kWheelKey, e16::kOpenKey, e16::kCfgPrevKey, e16::kCfgNextKey }) clean = clean && seen.insert (16 * 128 + num).second;
            for (int j = 0; j < kSlots * kCells; ++j) clean = clean && seen.insert (e16::kFxChannel * 128 + e16::kInsCc + j).second && seen.insert (e16::kFxChannel * 128 + e16::kInsPressCc + j).second;
            clean = clean && seen.insert (e16::kFxChannel * 128 + e16::kLenCc).second;         // (the generator's four are in already: every lane's are the same four, the active lane's)
            for (int j = 0; j < 4; ++j) clean = clean && seen.count (e16::kFxChannel * 128 + e16::kGenCc + j) == 1;
            for (int j = 0; j < e16::kCfgPer * e16::kCfgPages; ++j) clean = clean && seen.insert (e16::kFxChannel * 128 + e16::cfgCc (j)).second && e16::cfgAt (e16::cfgCc (j)) == j;
            // none on a number MIDI keeps for itself (Bank Select's two, Data Entry's two, the registered-parameter six, the channel-mode eight)
            for (int v : seen) { const int ch = v / 128, num = v % 128; if (ch == e16::kFxChannel || (ch == 16 && num >= e16::kWheelCc && num <= e16::kCfgNextKey)) clean = clean && num != 0 && num != 6 && num != 32 && num != 38 && ! (num >= 96 && num <= 101) && num < 120; }
            check (clean && e16::kPcChannel != e16::kFxChannel && e16::kPcChannel > kLanes && e16::kPcChannel < e16::kFindChannel,
                   "the second scene's addresses (the wheel, OPEN, an insert's places and their presses, the sequencer strip, CONFIG's 36) are no one else's, and the channel the page is turned on carries nothing else");
        }

        ticks (14);
        const juce::String atStart = turns();                           // (an E16 newly here is turned to what is in front, once: LEAD)
        out.clear();
        // brought to the front here: the E16 is turned there. Within a scene the page by Program Change alone (the page less one); to the other scene, Bank Select (its slot less one) first.
        ses.bringLane (2, 0); ticks (1);
        const juce::String t1 = turns(); out.clear(); ticks (1);
        ses.bringLane (5, 1); ticks (1);
        const juce::String t2 = turns(); out.clear(); ticks (1);
        ses.bring ({ Session::fInsert, 1 }, 1); ticks (1);
        const juce::String t3 = turns(); out.clear(); ticks (1);
        ses.bring ({ Session::fMaster, 0 }, 0); ticks (1);
        const juce::String t4 = turns(); out.clear(); ticks (1);
        ses.bring ({ Session::fSnap, 0 }, 0); ticks (1);
        const juce::String t5 = turns(); out.clear(); ticks (1);
        const bool noNote = midi.pageNote().isEmpty();
        out.clear();
        // (the snapshot knobs are the twelfth page of both scenes: from the lanes' scene they are its twelfth, with no scene sent)
        ses.bringLane (9, 0); ticks (1);
        const juce::String t6 = turns(); out.clear(); ticks (1);
        ses.bring ({ Session::fSnap, 0 }, 0); ticks (1);
        const juce::String t7 = turns(); out.clear(); ticks (1);
        check (atStart == "B0=0 B32=0 P7" && t1 == "P2" && t2 == "P5" && t3 == "B0=0 B32=1 P3" && t4 == "P6" && t5 == "P11" && t6 == "B0=0 B32=0 P9" && t7 == "P11" && noNote && midi.pageNote().isEmpty(),
               "a module brought to the front is the E16's page: an E16 newly here is sent the scene and the page of what is in front (" + atStart + "); within a scene the page is a Program Change by itself, OPEN HAT (" + t1 + "), BASS (" + t2
                   + "); Bank Select only to go to the other scene, BASS's second insert (" + t3 + "), then the master in that scene (" + t4 + "); the snapshot knobs are the twelfth page of whichever scene it is in (" + t5
                   + " from the second; a lane, " + t6 + ", and " + t7 + " from the first)");

        // A scene that is selected loads, and comes up on its first page whatever page came with the Bank Select: when its
        // script says so, the page is said again by itself. (Wiley's E16: every turn sent with a Bank Select ended on KICK's page.)
        {
            ses.bringLane (5, 0); ticks (3); out.clear();                // (from a lane's page, in the first scene)
            mockAnswers = false;                                         // (what this E16 says is written out below)
            const int frontWas = ses.frontCount;
            ses.bring ({ Session::fInsert, 2 }, 1); ticks (1);
            const juce::String sent1 = turns(); out.clear();
            juce::Thread::sleep (320);                                   // (the scene takes its time)
            ticks (1);
            answers (e16::kFocusB + e16::pgMap);                         // "the second scene, its first page"
            const juce::String again = turns(); out.clear();
            const bool notFollowed = ses.showing == 0 && ses.wantShowing != 1 && ses.front == Session::Front { Session::fInsert, 2 } && ses.frontCount == frontWas + 1;
            answers (e16::kFocusB + e16::pgIns + 2);
            const bool settled = midi.pageNote().isEmpty() && turns().isEmpty();
            juce::Thread::sleep (450); ticks (2);
            check (sent1 == "B0=0 B32=1 P4" && again == "P4" && notFollowed && settled && turns().isEmpty(),
                   "sent to another scene (" + sent1 + ") an E16 that comes up on that scene's first page is not followed there (the library does not open): the page is said again by itself (" + again + "), and when it says it is there nothing more is sent");
            out.clear();
            // one whose script says nothing: the page again a moment later, and once more
            ses.bringLane (1, 0); ticks (1);
            const juce::String back = turns(); out.clear();
            juce::Thread::sleep (420); ticks (1);
            const juce::String blind1 = turns(); out.clear();
            juce::Thread::sleep (820); ticks (1);
            const juce::String blind2 = turns(); out.clear();
            juce::Thread::sleep (1400); ticks (2);
            check (back == "B0=0 B32=0 P1" && blind1 == "P1" && blind2 == "P1" && turns().isEmpty() && midi.pageNote().contains ("NO ANSWER"),
                   "back to the first scene (" + back + ") with no answer from the E16: the page is said again by itself twice (" + blind1 + ", " + blind2 + "), and SETUP's E16 line says it did not answer");
            answers (2);                                                 // (it is there after all)
            mockAnswers = true;
            out.clear();
        }
        // (midi.json's "e16_scene_always": the scene said with every page, as it was)
        midi.setSceneAlways (true);
        ses.bringLane (3, 0); ticks (1);
        const juce::String a1 = turns(); out.clear(); ticks (1);
        midi.setSceneAlways (false);
        ses.bring ({ Session::fSeq, 0 }, 0); ticks (1);
        const juce::String a2 = turns(); out.clear(); ticks (1);
        ses.bring ({ Session::fMaster, 0 }, 0); ticks (1);
        const juce::String a3 = turns(); out.clear(); ticks (1);
        check (a1 == "B0=0 B32=0 P3" && a2 == "B0=0 B32=1 P7" && a3 == "P6", "with \"e16_scene_always\" the scene is said with a page of the scene it is in: " + a1 + "; without: " + a2 + ", " + a3);
        out.clear();

        // the slots the scenes are in, and off
        midi.setSceneSlot (0, 5); midi.setSceneSlot (1, 9);
        ses.bringLane (0, 0); ticks (1);
        const juce::String s1 = turns(); out.clear();
        ses.bring ({ Session::fReturns, 0 }, 0); ticks (1);
        const juce::String s2 = turns(); out.clear();
        midi.setPcChannel (0);
        ses.bringLane (3, 0); ticks (3);
        const juce::String s3 = turns(); out.clear();
        midi.setPcChannel (e16::kPcChannel); midi.setSceneSlot (0, 1); midi.setSceneSlot (1, 2);
        check (s1 == "B0=0 B32=4 P0" && s2 == "B0=0 B32=8 P5" && s3.isEmpty(), "the scenes' slots are SETUP's (5 and 9: " + s1 + ", " + s2 + "), and with the channel OFF the E16 is left alone");

        // the E16 turns its own page: what is in front follows, and nothing is sent back for it
        turnsTo (9); ticks (3);
        const bool laneFollows = ses.activeLane == 8 && ses.front.kind == Session::fLane && turns().isEmpty() && ccOut (9, 12) >= 0;
        out.clear();
        turnsTo (e16::kFocusB + e16::pgRet); ticks (4);
        int retVals = 0;
        for (int k = 0; k < 8; ++k) if (ccOut (16, ccx::kDub + k) >= 0) ++retVals;
        const bool retFollows = ses.front.kind == Session::fReturns && ses.activeLane == 8 && turns().isEmpty() && retVals == 8
                             && ccOut (16, ccx::kDub + 1) == e16::to7 (e.control (cx::kRet + 1), e.target (cx::kRet + 1));
        out.clear();
        turnsTo (e16::kFocusB + e16::pgMaster); ticks (4);
        bool masterFollows = ses.front.kind == Session::fMaster && turns().isEmpty() && ccOut (16, ccx::kMaster) >= 0 && ccOut (16, ccx::kMaster + 2) >= 0;
        for (int k = 0; k < kBusKnobs; ++k) masterFollows = masterFollows && ccOut (16, ccx::kBus + k) == e16::to7 (e.control (cx::kBus + k), e.target (cx::kBus + k));
        for (int k = 0; k < kPkGlobals; ++k) masterFollows = masterFollows && ccOut (16, ccx::kPkG + k) == e16::to7 (e.control (cx::kPkG + k), e.target (cx::kPkG + k));
        in (16, ccx::kMaster + 1, 99); in (16, ccx::kPkG, 40); ticks (1);
        const bool amountTurned = std::abs (e.target (cx::kPkG) - 40.0f / 127.0f) < 1.0e-6f;
        in (16, ccx::kPkG, 0); in (6, ccx::kPk, 90); ticks (1);                 // (and a lane's pocket has addresses on the lane's channel: BASS's HOME)
        const bool homeTurned = std::abs (e.target (cx::pk (5, 0)) - 90.0f / 127.0f) < 1.0e-6f && e.target (cx::kPkG) == 0.0f;
        ses.userSet (cx::pk (5, 0), e.control (cx::pk (5, 0)).def);
        check (laneFollows && retFollows && masterFollows && amountTurned && homeTurned && std::abs (e.target (cx::kMaster + 1) - 99.0f / 127.0f) < 1.0e-6f,
               "a page the E16 turns to itself brings its module to the front (CHORD; the returns; the master) with no page sent back, and the page is told its values (the returns' eight; the master's three, its drum bus's four and POCKETS' four, whose AMOUNT is turned from there)");
        out.clear();

        // an insert's page: the active lane's, whatever effect is in the slot
        while (e.insertType (8, 0) != 0) ses.stepInsert (8, 0);         // DELAY in CHORD's first slot
        turnsTo (e16::kFocusB + e16::pgIns); ticks (4);
        int insVals = 0;
        for (int c = 0; c < kCells; ++c) if (ccOut (13, e16::kInsCc + c) >= 0) ++insVals;
        const juce::String insTitle = said (5), n1 = name (1), n2 = name (2), n5 = name (5), n13 = name (13);
        out.clear();
        in (13, e16::kInsCc + 1, 100); ticks (1);                        // FEEDBACK
        const bool fbk = std::abs (e.target (cx::ins (8, 0, 1)) - 100.0f / 127.0f) < 1.0e-6f && ccOut (13, e16::kInsCc + 1) < 0;
        in (13, e16::kInsPressCc + 4, 127);                              // THROW, held
        const bool thrown = e.target (cx::ins (8, 0, 4)) > 0.5f;
        in (13, e16::kInsPressCc + 4, 0);
        const bool let = e.target (cx::ins (8, 0, 4)) < 0.5f;
        press (13, e16::kInsPressCc + 5);                                // FREEZE, switched
        const bool frozen = e.target (cx::ins (8, 0, 5)) > 0.5f;
        press (13, e16::kInsPressCc + 5);
        const bool thawed = e.target (cx::ins (8, 0, 5)) < 0.5f;
        press (13, e16::kInsPressCc + 1);                                // (a knob's press is nothing)
        ticks (1);
        check (ses.front == Session::Front { Session::fInsert, 0 } && insVals == 8 && insTitle == "CRD 1 DELAY" && n1 == "TIME" && n2 == "FDBK" && n5 == "THRW" && n13.isEmpty()
                   && fbk && thrown && let && frozen && thawed && std::abs (e.target (cx::ins (8, 0, 1)) - 100.0f / 127.0f) < 1.0e-6f,
               "INS 1 is the active lane's first insert: told its eight values and named (" + insTitle + ": " + n1 + ", " + n2 + " ... " + n5 + "); FEEDBACK turned, THROW held by its knob's press, FREEZE switched by its own");
        out.clear();

        // the sequencer strip
        turnsTo (e16::kFocusB + e16::pgSeq); ticks (4);
        const bool seqTold = ccOut (13, e16::kLenCc) == ses.pattern (8).length - 1 && ccOut (13, e16::kGenCc) >= 0;
        in (13, e16::kLenCc, 31); in (13, e16::kGenCc + 1, 64); ticks (1);
        check (ses.front.kind == Session::fSeq && seqTold && ses.pattern (8).length == 32 && std::abs (e.target (cx::gen (8, 1)) - e16::from7 (e.control (cx::gen (8, 1)), 64)) < 1.0e-6f,
               "SEQ is the active lane's sequencer strip: LENGTH (the number and one: 32 steps) and the generator's four knobs, which had no address before");
        out.clear();

        // OPEN: CONFIG for what is in front, on the E16 its controls twelve to a page, named
        ses.bringLane (5, 0); ticks (3); out.clear();                   // BASS
        press (16, e16::kOpenKey); ticks (1);
        const juce::String toCfg = turns();
        ticks (4);
        const auto list = ses.configList();
        int knobAt = -1, listAt = -1, switchAt = -1;
        for (int i = 0; i < (int) list.size() && i < e16::kCfgPer; ++i)
        {
            if (knobAt < 0 && list[(size_t) i].kind == Kind::Knob) knobAt = i;
            if (listAt < 0 && list[(size_t) i].kind == Kind::List && list[(size_t) i].choices.size() > 2) listAt = i;
            if (switchAt < 0 && list[(size_t) i].kind == Kind::Toggle) switchAt = i;
        }
        const juce::String cfgTitle = said (5);
        bool named = (int) list.size() > e16::kCfgPer, told = true;
        for (int i = 0; i < e16::kCfgPer && i < (int) list.size(); ++i)
        {
            named = named && name (i + 1) == list[(size_t) i].name.removeCharacters (" ").toUpperCase().substring (0, 4).trim();
            told = told && (list[(size_t) i].kind == Kind::Hold || ccOut (13, e16::cfgCc (i)) >= 0);
        }
        out.clear();
        bool turned = knobAt >= 0 && listAt >= 0;
        if (turned)
        {
            in (13, e16::cfgCc (knobAt), 90);
            const int n = list[(size_t) listAt].choices.size();
            in (13, e16::cfgCc (listAt), 127);                          // a list: the top of the knob is its last choice
            ticks (1);
            juce::Thread::sleep (170); ticks (7);
            const auto now = ses.configList();
            turned = std::abs (now[(size_t) knobAt].value - 90.0f / 127.0f) < 1.0e-3f && std::abs (now[(size_t) listAt].value - 1.0f) < 1.0e-6f
                  && ccOut (13, e16::cfgCc (knobAt)) < 0 && ccOut (13, e16::cfgCc (listAt)) < 0;        // (neither is sent back: each is where the hand put it)
            // and one set on the screen is sent, to the middle of its share
            e.setSetup (ses.cfg, list[(size_t) listAt].id, 0.0f);
            ticks (7);
            turned = turned && ccOut (13, e16::cfgCc (listAt)) == 64 / n;
        }
        out.clear();
        press (16, e16::kCfgNextKey); ticks (1);
        const juce::String toPage2 = turns();
        ticks (4);
        const juce::String title2 = said (5), first2 = name (1);
        out.clear();
        press (16, e16::kCfgPrevKey); ticks (1); press (16, e16::kCfgPrevKey); ticks (1);
        const juce::String toPage1 = turns(); out.clear();
        press (16, e16::kOpenKey); ticks (1);
        const juce::String shut = turns(); out.clear();
        check (toCfg == "B0=0 B32=1 P8" && cfgTitle == "BAS CFG 1/" + juce::String (((int) list.size() + 11) / 12) && named && told && turned && toPage2 == "P9" && title2.startsWith ("BAS CFG 2/")
                   && first2 == list[12].name.removeCharacters (" ").toUpperCase().substring (0, 4).trim() && toPage1 == "P8" && shut == "B0=0 B32=0 P5" && ! ses.cfgOpen,
               "OPEN on BASS opens its CONFIG and turns the E16 to it (" + toCfg + ", " + cfgTitle + ": " + juce::String ((int) list.size()) + " controls, twelve to a page, each named and told its value); a knob and a list are turned from it and left where the hand put them; PAGE + and PAGE - ("
                   + toPage2 + ", " + toPage1 + "); OPEN again shuts it and the E16 is back on BASS (" + shut + ")");

        // the returns have two: A, then B, then shut. The master's is its drum bus's.
        ses.bring ({ Session::fReturns, 0 }, 2); ticks (2); out.clear();
        press (16, e16::kOpenKey); ticks (1);
        const bool openA = ses.cfgOpen && ses.cfg.kind == 2 && ses.cfg.slot == 0;
        press (16, e16::kOpenKey); ticks (1);
        const bool openB = ses.cfgOpen && ses.cfg.kind == 2 && ses.cfg.slot == 1;
        press (16, e16::kOpenKey); ticks (1);
        const bool shutAgain = ! ses.cfgOpen;
        ses.bring ({ Session::fMaster, 0 }, 2); ticks (2);
        press (16, e16::kOpenKey); ticks (4);
        const bool openM = ses.cfgOpen && ses.cfg.kind == 3;
        const juce::String busTitle = said (5);
        const auto busList = ses.configList();
        bool lanesNamed = false;
        for (auto& sp : busList) lanesNamed = lanesNamed || (sp.group == "LANES" && sp.name == "CLOSED HAT" && sp.value > 0.5f);
        press (16, e16::kOpenKey); ticks (2);
        check (openA && openB && shutAgain && openM && busTitle.startsWith ("DRM BUS CFG") && lanesNamed && ! ses.cfgOpen,
               "OPEN on the returns: DUB's CONFIG, SPACE's, shut. On the master: its drum bus's (" + busTitle + ", " + juce::String ((int) busList.size()) + " controls, the lanes by name), and shut");

        // Behind the master's CONFIG is POCKETS, its page: that second OPEN asked for it. While it is
        // up the CONFIG pages are its knobs: the eleven lanes' HOME and AMOUNT, their WIDTH and
        // CLEAR, their YIELD and SPEED; and a fourth, their HOLD and RETURNS, on the third's knobs.
        {
            const bool asked = ses.wantShowing == 3 && ses.pocketsUp();         // (there is no screen here to take it up: asked for is up)
            ticks (5);
            const juce::String pkTitle = said (5), k1 = name (1), k6 = name (6), k11 = name (11), k12 = name (12);
            const auto pkList = ses.configList();
            bool told = (int) pkList.size() == Session::kPocketList;
            for (int i = 0; i < e16::kCfgPer; ++i) told = told && ccOut (13, e16::cfgCc (i)) == (int) std::lround (e.target (Session::pocketControl (i)) * 127.0f);
            out.clear();
            ses.userSet (cx::kGlide, 0.0f);
            in (13, e16::cfgCc (5), 100); in (13, e16::cfgCc (11), 64); ticks (1);         // BASS's HOME; AMOUNT
            const bool turned = std::abs (e.target (cx::pk (5, 0)) - 100.0f / 127.0f) < 1.0e-6f && std::abs (e.target (cx::kPkG) - 64.0f / 127.0f) < 1.0e-6f;
            out.clear();
            press (16, e16::kCfgNextKey); ticks (5);
            const juce::String to2 = turns(), title2 = said (5);
            in (13, e16::cfgCc (12 + 9), 20); ticks (1);                                    // the PAD's WIDTH
            out.clear();
            press (16, e16::kCfgNextKey); ticks (5);
            const juce::String to3 = turns(), title3 = said (5), twelfth3 = name (12);
            out.clear();
            press (16, e16::kCfgNextKey); ticks (5);                                        // the fourth: on the third's knobs
            const juce::String to4 = turns(), title4 = said (5), twelfth4 = name (12);
            const bool retold = ccOut (13, e16::cfgCc (24)) == (int) std::lround (e.target (cx::pk (0, 2)) * 127.0f);        // (the knobs under the hand are told what they now are: the KICK's HOLD)
            out.clear();
            in (13, e16::cfgCc (24 + 10), 33); in (13, e16::cfgCc (24 + 11), 99); ticks (1);  // the VOICE's HOLD; RETURNS
            const bool fourth = std::abs (e.target (cx::pk (10, 2)) - 33.0f / 127.0f) < 1.0e-6f && std::abs (e.target (cx::kPkG + 3) - 99.0f / 127.0f) < 1.0e-6f
                             && std::abs (e.target (cx::pk (10, 3)) - e.control (cx::pk (10, 3)).def) < 1.0e-6f;
            press (16, e16::kCfgPrevKey); ticks (5);
            in (13, e16::cfgCc (24 + 10), 50); ticks (1);                                     // (back on the third: the VOICE's YIELD)
            const bool third = std::abs (e.target (cx::pk (10, 3)) - 50.0f / 127.0f) < 1.0e-6f && std::abs (e.target (cx::pk (9, 1)) - 20.0f / 127.0f) < 1.0e-6f;
            out.clear();
            press (16, e16::kOpenKey); ticks (3);
            const juce::String back = turns();
            check (asked && pkTitle == "POCKETS HOME" && k1 == "KCK" && k6 == "BAS" && k11 == "VOX" && k12 == "AMNT" && told && turned
                       && to2 == "P9" && title2 == "POCKETS WIDTH" && to3 == "P10" && title3 == "POCKETS YIELD" && twelfth3 == "SPD"
                       && to4.isEmpty() && title4 == "POCKETS HOLD" && twelfth4 == "RETS" && retold && fourth && third && back == "P6" && ! ses.pocketsUp(),
                   "OPEN once more on the master is POCKETS: the CONFIG pages are then its knobs, a lane to a knob (" + pkTitle + ": " + k1 + " ... " + k6 + " ... " + k11 + ", " + k12 + "), told their values; BASS's HOME and AMOUNT are turned from the first, "
                       "the PAD's WIDTH from the second (" + title2 + "), and PAGE + from the third (" + title3 + ") is a fourth on the same knobs (" + title4 + ": no page is turned, the knobs are named and told again); OPEN leaves it, back to the master's page (" + back + ")");
            for (int l = 0; l < kLanes; ++l) for (int k = 0; k < kPkLane; ++k) ses.userSet (cx::pk (l, k), e.control (cx::pk (l, k)).def);
            for (int k = 0; k < kPkGlobals; ++k) ses.userSet (cx::kPkG + k, e.control (cx::kPkG + k).def);
            ses.wantShowing = -1;
            ticks (3);
        }
        out.clear();

        // the pointer's wheel: knob 13 pressed takes it up, its turns go through the stops, and a second press is the choice
        using S = Session;
        for (int l = 0; l < kLanes; ++l) for (int sl = 0; sl < kSlots; ++sl) e.setInsertType (l, sl, (l == 0 && sl != 1) ? 0 : -1);     // (an effect in the kick's first and third inserts, and nowhere else)
        ses.bringLane (0, 0); ticks (3);                                // the E16 is on KICK's page
        juce::Thread::sleep (310); out.clear();
        const int sendA = cx::lane (0, cx::kSendA);
        const float sendWas = e.target (sendA);
        const int stopsNow = (int) ses.stops().size();
        press (16, e16::kWheelKey); ticks (1);
        const bool started = midi.wheelInUse() && ses.pointing && ses.pointAt == S::Stop { S::fLane, 0, 0 };
        const int placed = ccOut (1, e16::kKnobCc + 12);                 // its number, sent at once (the hand is not turning it yet): one to a click, the list in the middle of its travel
        const int placeFor0 = (127 - 3 * (stopsNow - 1)) / 2;
        const juce::String wheelTitle = said (5), sel = name (13);
        for (int v = placed + 1; v <= placed + 3; ++v) { in (1, e16::kKnobCc + 12, v); ticks (1); }
        const bool itsInsert = ses.pointAt == S::Stop { S::fInsert, 0, 0 } && ses.shownLane() == 0;
        const juce::String title1 = said (5);
        for (int v = placed + 4; v <= placed + 6; ++v) in (1, e16::kKnobCc + 12, v);
        ticks (1);
        const bool itsThird = ses.pointAt == S::Stop { S::fInsert, 0, 2 };                 // (its second has nothing in it)
        for (int v = placed + 7; v <= placed + 18; ++v) in (1, e16::kKnobCc + 12, v);
        ticks (1);
        const bool fifth = ses.pointAt == S::Stop { S::fLane, 4, 0 } && ses.shownLane() == 4;
        juce::Thread::sleep (700); ticks (2);
        // (rested on for most of a second: still nothing is in front but what was, and the E16 has not been turned)
        const bool onlyPointed = ses.front.kind == S::fLane && ses.activeLane == 0 && e.target (sendA) == sendWas && turns().isEmpty() && midi.wheelInUse();
        out.clear();
        press (16, e16::kWheelKey); ticks (1);
        const juce::String chosen = turns();
        const bool chose = ! midi.wheelInUse() && ! ses.pointing && ses.activeLane == 4 && ses.front.kind == S::fLane && ses.frontBy == 2;
        ticks (2);
        const bool given = ccOut (1, e16::kKnobCc + 12) == e16::to7 (e.control (sendA), sendWas);     // (SEND A's own value again)
        check (started && stopsNow == kLanes + 2 + 4 && placed == placeFor0 && placed >= 11 && wheelTitle == "> 1 KICK" && sel == "SEL" && itsInsert && title1 == "> 1.1 DELAY" && itsThird && fifth && onlyPointed && chosen == "P4" && chose && given,
               "knob 13 pressed takes the pointer up: three clicks to the next stop, a lane and then its inserts that have an effect in them (" + title1 + "), then the next lane; nothing comes to the front and the E16 stays where it is, however long the pointer rests; "
                   "pressed again, what it is on comes to the front and the E16 turns to it (" + chosen + "); SEND A was never moved and is told its own value again");
        out.clear();

        // a number the E16 did not take does not throw the pointer: turns are counted from where the knob says it is
        e.set (cx::lane (4, cx::kSendA), 0.0f, 0.0);                    // (PERC's SEND A at nought: so is its knob 13)
        juce::Thread::sleep (310); ticks (3); out.clear();
        press (16, e16::kWheelKey); ticks (1);
        const int toldAt = ccOut (5, e16::kKnobCc + 12);                 // (told to stand at PERC's place among the stops)
        in (5, e16::kKnobCc + 12, 1); ticks (1);                         // ... and it says 1: it was at nought, did not take the number, and has been turned a click
        const bool notThrown = ses.pointAt == S::Stop { S::fLane, 4, 0 };
        for (int v = 2; v <= 4; ++v) { in (5, e16::kKnobCc + 12, v); ticks (1); }
        const bool counted = ses.pointAt == S::Stop { S::fLane, 5, 0 };
        out.clear();
        juce::Thread::sleep (160); ticks (1);
        const int toldAgain = ccOut (5, e16::kKnobCc + 12);              // (it is a long way from where it should be: told again, now that the hand has stopped)
        in (5, e16::kKnobCc + 12, toldAgain - 4); ticks (1);             // this time it took it: back four clicks
        const bool backOne = ses.pointAt == S::Stop { S::fLane, 4, 0 };
        out.clear();
        juce::Thread::sleep (600); ticks (2);
        const bool leftAlone = ccOut (5, e16::kKnobCc + 12) < 0;        // (it is where it should be: nothing is sent to a knob in a hand)
        // another knob turned: the pointer is put down and nothing is chosen
        in (5, e16::kKnobCc + 2, 50); ticks (1);
        const bool putDown = ! midi.wheelInUse() && ! ses.pointing && ses.activeLane == 4 && ses.front.kind == S::fLane;
        check (toldAt == placeFor0 + 3 * 6 && notThrown && counted && toldAgain == toldAt + 3 && backOne && leftAlone && putDown,
               "a knob that did not take the number it was told (told " + juce::String (toldAt) + ", it says 1) does not throw the pointer and its clicks still count; told again at a pause it takes it, and is then left alone; another knob turned puts the pointer down with nothing chosen");
        out.clear();

        // a made-up knob under the wheel, turned this way and that for a while: it keeps a number from 0 to 127, stops at
        // each end, says its number when it moves, and takes a number it is told (or, the second time round, never does)
        {
            juce::String walked;
            bool allRight = true;
            for (int takes = 1; takes >= 0; --takes)
            {
                e.set (cx::lane (0, cx::kSendA), takes ? 0.0f : 0.5f, 0.0);     // (KICK's knob 13 at nought; for the knob that takes nothing, in the middle, where it has room)
                ses.bringLane (0, 0); ticks (3);
                juce::Thread::sleep (310); ticks (3);
                int knob = ccOut (1, e16::kKnobCc + 12);                 // (where SF-000 has its ring, as SEND A)
                if (knob < 0) knob = e16::to7 (e.control (cx::lane (0, cx::kSendA)), e.target (cx::lane (0, cx::kSendA)));
                out.clear();
                auto hear = [&] { if (takes) { const int v = ccOut (1, e16::kKnobCc + 12); if (v >= 0) knob = v; } out.clear(); };
                press (16, e16::kWheelKey); ticks (1); hear();
                const auto list = ses.stops();
                int idx = 0, rest = 0, tellings = 0;
                juce::Random r (takes ? 41 : 43);
                for (int go = 0; go < 36; ++go)
                {
                    const int dir = r.nextInt (5) < 3 ? 1 : -1, clicks = 1 + r.nextInt (takes ? 9 : 4);
                    for (int c = 0; c < clicks; ++c)
                    {
                        const int to = juce::jlimit (0, 127, knob + dir);
                        if (to == knob) continue;                        // (at the end of its travel it has nothing to say)
                        knob = to;
                        in (1, e16::kKnobCc + 12, knob); ticks (1);
                        rest += dir;
                        const int steps = rest / 3;
                        rest -= steps * 3;
                        const int at = juce::jlimit (0, (int) list.size() - 1, idx + steps);
                        if (at != idx + steps) rest = 0;
                        idx = at;
                    }
                    if (go % 6 == 5) { juce::Thread::sleep (160); ticks (1); if (ccOut (1, e16::kKnobCc + 12) >= 0) ++tellings; hear(); }
                }
                const bool right = midi.wheelInUse() && ses.pointAt == list[(size_t) idx] && ses.activeLane == 0 && ses.front.kind == S::fLane;
                allRight = allRight && right && (takes ? tellings <= 3 : true);
                walked += juce::String (takes ? "one that takes its number: " : "; one that never does: ") + ses.stopName (ses.pointAt) + (right ? "" : " (should be " + ses.stopName (list[(size_t) idx]) + ")")
                        + ", told " + juce::String (tellings) + " times at 6 pauses";
                in (1, e16::kKnobCc + 2, 50); ticks (2);                 // (put down)
                out.clear();
            }
            e.set (cx::lane (0, cx::kSendA), 0.0f, 0.0); ticks (3); out.clear();
            check (allRight, "a knob turned this way and that under the wheel leaves the pointer where its clicks put it, with nothing chosen (" + walked + ")");
        }

        // on a page where knob 13 is nothing else, a turn takes the pointer up; OPEN with it up chooses first
        ses.bringStop ({ S::fInsert, 0, 0 }, 1); ticks (4);
        juce::Thread::sleep (310); ticks (1);
        const int own = ccOut (16, e16::kWheelCc);                       // (it stands at what is in front)
        in (16, e16::kWheelCc, own + 3); ticks (1);
        const bool tookUp = midi.wheelInUse() && ses.pointAt == S::Stop { S::fInsert, 0, 2 } && ses.front == S::Front { S::fInsert, 0 };
        press (16, e16::kOpenKey); ticks (1);
        const bool openedIt = ! midi.wheelInUse() && ses.front == S::Front { S::fInsert, 2 } && ses.cfgOpen && ses.cfg.kind == 1 && ses.cfg.lane == 0 && ses.cfg.slot == 2;
        press (16, e16::kOpenKey); ticks (1);
        check (own >= 0 && tookUp && openedIt && ! ses.cfgOpen, "on a page where knob 13 is the wheel and nothing else a turn takes the pointer up; OPEN with the pointer up chooses what it is on and opens its CONFIG");
        out.clear();

        // a knob turned on a page the E16 never said it was on: it is taken to be there, and nothing comes to the front
        ticks (1);                                                      // (its answer to the last turn is in)
        juce::Thread::sleep (310);
        const int frontWas = ses.frontCount;
        mockScene = 0;                                                  // (turned by hand to the first scene's ninth page, with no script to say so)
        in (9, e16::kKnobCc + 2, 70); ticks (4);
        const bool quiet = ccOut (9, e16::kKnobCc + 2) < 0 && turns().isEmpty() && ses.frontCount == frontWas;
        ses.bringLane (8, 0); ticks (2);                                // (the module of that page, brought to the front on the screen: the E16 is there already)
        const bool thereAlready = turns().isEmpty();
        ses.bringLane (7, 0); ticks (2);
        check (quiet && thereAlready && turns() == "P7",
               "a knob turned on a page the E16 has not said it is on: it is taken to be on that page, nothing is brought to the front or sent back for it, and it is not sent there again when that module comes to the front");
        out.clear();

        // pressed again with the pointer where it was: the same is chosen, and the page has its own names back
        juce::Thread::sleep (310);
        ses.bringLane (3, 0); ticks (3); out.clear();
        press (16, e16::kWheelKey); ticks (1);
        const bool again = midi.wheelInUse() && said (5) == "> 4 CLAP" && name (13) == "SEL";
        out.clear();
        press (16, e16::kWheelKey); ticks (1);
        const bool namesBack = said (5).isEmpty() && name (13).isEmpty() && said (4).length() == 65;
        // the snapshot page, where a press recalls: knob 13 held is the wheel, and a press let go at once still recalls
        turnsTo (12); ticks (3);
        {
            std::vector<juce::int64> two;                               // (two that are not the base, so that a recall of either shows)
            for (auto& sn : ses.library) if (sn.id != ses.baseId && two.size() < 2) two.push_back (sn.id);
            ses.knobs[12].snaps = two;
        }
        ses.turnSnapKnob (12, 1.0f);
        const auto baseWas = ses.baseId;
        in (16, e16::kPressCc + 12, 127); ticks (1);
        const bool notYet = ses.baseId == baseWas && ! midi.wheelInUse();
        in (16, e16::kPressCc + 12, 0); ticks (1);
        const bool recalled = ses.baseId != baseWas && ! midi.wheelInUse();
        const auto baseThen = ses.baseId;
        in (16, e16::kPressCc + 12, 127); juce::Thread::sleep (420); ticks (1);
        const bool heldIs = midi.wheelInUse() && ses.pointing && ses.pointAt.kind == Session::fSnap;
        in (16, e16::kPressCc + 12, 0); ticks (1);
        const bool noRecall = ses.baseId == baseThen && midi.wheelInUse();
        // OPEN on the snapshot knobs is the library; in the library it is the way back
        press (16, e16::kWheelKey); ticks (1);
        // (the scene now has knob 13's press on the snapshot page as the wheel's own key, as on a lane's page: at once, and its turns are the pointer's)
        const bool keyOff = ! midi.wheelInUse();
        const float snap13 = ses.value (cx::kSnap + 12);
        press (16, e16::kWheelKey); ticks (1);
        const int stood = ccOut (16, e16::kKnobCc + 12);
        for (int v = stood - 1; v >= stood - 3; --v) { in (16, e16::kKnobCc + 12, v); ticks (1); }
        const bool byKey = keyOff && midi.wheelInUse() && ses.pointing && ses.pointAt == Session::Stop { Session::fLane, kLanes - 1, 0 } && ses.value (cx::kSnap + 12) == snap13 && ses.front.kind == Session::fSnap;
        in (16, e16::kKnobCc + 2, 0); ticks (2);                         // (another knob: the pointer is put down)
        out.clear();
        in (16, e16::kPressCc + 13, 127); juce::Thread::sleep (420); ticks (1); in (16, e16::kPressCc + 13, 0); ticks (1);
        const bool asksLibrary = ses.wantShowing == 1;
        ses.showing = 1; ses.wantShowing = -1; ticks (1);               // (the screen takes it up)
        const juce::String toMap = turns(); out.clear();
        in (15, e16::kPressCc + 13, 127); juce::Thread::sleep (420); ticks (1); in (15, e16::kPressCc + 13, 0); ticks (1);
        const bool asksBack = ses.wantShowing == 0;
        ses.showing = 0; ses.wantShowing = -1; ticks (1);
        const juce::String fromMap = turns(); out.clear();
        // MOTION: knob 15 held on the snapshot knobs. The macro designer is asked for, where MOTION is, and
        // the E16 turns to its knobs (CONFIG's first page): named, told, turned from there. OPEN is the way back.
        {
            const auto base15 = ses.baseId;
            in (16, e16::kPressCc + 14, 127); juce::Thread::sleep (420); ticks (1); in (16, e16::kPressCc + 14, 0); ticks (1);
            const bool asks = ses.wantShowing == 2 && ses.motionUp && ses.baseId == base15;        // (and nothing was recalled by it)
            ses.showing = 2; ses.wantShowing = -1; ticks (6);
            const juce::String to = turns(), title = said (5), n1 = name (1), n2 = name (2), n3 = name (3), n4 = name (4), n9 = name (9);
            const bool told = ccOut (13, e16::cfgCc (1)) >= 0 && ccOut (13, e16::cfgCc (3)) == 64 && (int) ses.configList().size() == cx::kMotUnits * cx::kMotKnobs;
            out.clear();
            in (13, e16::cfgCc (0), 16); in (13, e16::cfgCc (2), 30); in (13, e16::cfgCc (3), 96); in (13, e16::cfgCc (4 + 1), 127); ticks (1);
            const bool set = e.valueText (cx::mot (0, 0)) == "SINE" && e.valueText (cx::mot (0, 2)) == "4" && std::abs (e.target (cx::mot (0, 3)) - (0.5f + 32.0f / 126.0f)) < 1.0e-5f
                          && e.valueText (cx::mot (1, 1)) == Engine::motionSpeeds()[Engine::motionSpeeds().size() - 1] && ses.motionOn (3) == 1;
            out.clear();
            press (16, e16::kOpenKey); ticks (1);
            const bool asksBack2 = ses.wantShowing == 0;
            ses.showing = 0; ses.wantShowing = -1; ticks (4);
            const juce::String from = turns(); out.clear();
            // the designer opened on the screen: the E16 is on the snapshot knobs, as it was, until it is turned to CONFIG's page by hand
            ses.showing = 2; ticks (4);
            const juce::String shown = turns(); out.clear();
            answers (e16::kFocusB + e16::pgCfg); ticks (4);
            const bool byHand = ses.motionUp && ses.showing == 2 && ses.wantShowing < 0 && said (5) == "KNOB MOTION";
            ses.showing = 0; ticks (4); out.clear();
            for (int i = cx::kMot; i < cx::kSnap; ++i) ses.userSet (i, e.control (i).def);
            ses.bring ({ Session::fSnap, 0 }, 2); ticks (4); out.clear();
            check (asks && to == "P8" && title == "KNOB MOTION" && n1 == "WAVE" && n2 == "SPD" && n3 == "KNOB" && n4 == "AMNT" && n9 == "WAVE" && told && set && asksBack2 && from == "P11" && ! ses.motionUp
                       && shown.isEmpty() && byHand,
                   "knob 15 held on the snapshot knobs is MOTION: the macro designer is asked for and the E16 turns to its knobs (" + to + ", " + title + ": " + n1 + " " + n2 + " " + n3 + " " + n4 + " for each of the three), told their values; "
                       "a wave, a knob, an amount and a speed are set from there; OPEN is the way back (" + from + "). The designer opened on the screen leaves the E16 on the snapshot knobs, and CONFIG's page turned to by hand there is MOTION");
        }
        // (back from the library the snapshot knobs are that scene's own twelfth page: no scene is loaded for them)
        in (16, e16::kKnobCc + 3, 40); ticks (2);                        // a snapshot knob turned there: still the second scene's twelfth page
        ses.bring ({ Session::fMaster, 0 }, 0); ticks (1);
        const juce::String toMaster = turns(); out.clear(); ticks (1);
        answers (e16::kFocusB + e16::kSnapPage); ticks (1);              // turned to it by hand in the second scene: the snapshot knobs come to the front
        const bool handTurn = ses.front.kind == Session::fSnap && turns().isEmpty();
        ses.turnSnapKnob (3, 0.0f);
        check (byKey, "on the snapshot page knob 13's press is the wheel's key too: the pointer is up at once on the snapshot knobs, a turn back takes it to VOICE, and snapshot knob 13 is not moved");
        check (again && namesBack && ! midi.wheelInUse() && notYet && recalled && heldIs && noRecall && asksLibrary && toMap == "B0=0 B32=1 P0" && asksBack && fromMap == "P11" && toMaster == "P6" && handTurn,
               "on the snapshot page knob 13 held for half a second is the wheel, and a press let go before that still recalls (as it is let go); knob 14 held there opens the library (" + toMap + ") and held in the library shuts it, to that scene's own snapshot page ("
                   + fromMap + "); a snapshot knob turned there leaves the E16 taken to be in that scene (the master next: " + toMaster + "), and its twelfth page turned to by hand brings the snapshot knobs to the front");
        ses.bringLane (3, 0); ticks (3); out.clear();

        // the E16 sent to one page says it is on another: nothing follows it there, the page is said again, and SETUP says so if it never comes round
        ticks (2);
        juce::Thread::sleep (310);
        mockAnswers = false;
        out.clear();
        ses.bringLane (2, 0); ticks (1);
        in (16, e16::kFocusCc, 4); ticks (1);
        const juce::String twice = turns();
        const bool noNoteYet = ses.activeLane == 2 && midi.pageNote().isEmpty();
        juce::Thread::sleep (920); ticks (1);
        const bool noted = ses.activeLane == 2 && midi.pageNote().contains ("ASKED FOR SF-000 PAGE 3") && midi.pageNote().contains ("PAGE 4");
        ses.bringLane (6, 0); ticks (1);
        const int countWas = ses.frontCount;
        in (16, e16::kFocusCc, 7); ticks (1);
        const bool cleared = midi.pageNote().isEmpty() && ses.activeLane == 6 && ses.frontCount == countWas && ses.frontBy == 0;
        // one answer late: the E16 says the page it was sent to a moment ago, when it has since been sent on. It is not followed back.
        ses.bringLane (9, 0); ticks (1);
        ses.bringLane (10, 0); ticks (1);
        in (16, e16::kFocusCc, 10); ticks (1);                           // (its answer to the first of the two)
        const bool notBack = ses.activeLane == 10;
        in (16, e16::kFocusCc, 11); ticks (1);
        const bool onward = ses.activeLane == 10 && midi.pageNote().isEmpty();
        // one that says nothing to a turn within its scene (a knob held down, a message lost): the page again, twice, and then SETUP says so
        out.clear();
        ses.bringLane (4, 0); ticks (1);
        juce::Thread::sleep (260); ticks (1);
        juce::Thread::sleep (460); ticks (1);
        const juce::String thrice = turns();
        const bool quietYet = midi.pageNote().isEmpty();
        juce::Thread::sleep (420); ticks (1);
        const bool unanswered = midi.pageNote().contains ("ASKED FOR SF-000 PAGE 5: NO ANSWER") && turns() == thrice && ses.activeLane == 4;
        // one whose PC Mode is Scene: a Program Change by itself took it to the other scene. Not followed, and SETUP says what to set.
        ses.bringLane (7, 0); ticks (1);
        in (16, e16::kFocusCc, e16::kFocusB + e16::pgMap); ticks (1);
        juce::Thread::sleep (1120); ticks (1);
        const bool pcModeSaid = midi.pageNote().contains ("IT WENT TO SF-FX MAP") && midi.pageNote().contains ("PC MODE MUST BE PAGE") && ses.activeLane == 7 && ses.showing == 0 && ses.wantShowing != 1;
        // (midi.json's "e16_page_first": 1 for an E16 that counts its pages from one in a Program Change)
        out.clear();
        midi.setPageFirst (1);
        ses.bringLane (5, 0); ticks (1);
        const juce::String oneUp = turns();
        midi.setPageFirst (0);
        in (16, e16::kFocusCc, 6); ticks (1);
        mockAnswers = true;
        check (thrice == "P4 P4 P4" && quietYet && unanswered && pcModeSaid && oneUp == "B0=0 B32=0 P6" && midi.pageNote().isEmpty(),
               "a turn within the scene that is not answered is said twice more (" + thrice + "), and then SETUP says it was not answered; one that ends in the other scene is not followed, and SETUP says the E16's PC Mode must be Page; "
               "an answer clears that; with \"e16_page_first\" 1 the page's own number is sent (" + oneUp + ")");
        check (twice == "P2 P2" && noNoteYet && noted && cleared && notBack && onward,
               "an E16 that goes somewhere other than where it was sent is not followed there: the page is said again (" + twice + "), and if it never comes round SETUP says which page was asked for and which it went to; one that agrees clears it, and brings nothing to the front a second time; "
               "an answer that comes after it has been sent on does not bring the last module back");
        out.clear();
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
        Engine eFx;
        juce::StringArray fxNames;
        for (int k = 0; k < 16; ++k) fxNames.add (k == 0 ? "ENERGY" : k == 5 ? "OPEN UP" : "");
        const auto fxScene = e16::fxText (eFx, fxNames);
        const auto lib = juce::JSON::parse (fxScene);
        bool form = lib.isObject() && lib["title"].toString() == "SF-FX" && lib["pages"].isArray() && lib["pages"].size() == 12;
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
        check (form, "the second scene is written, the library first: MAP on channel 15, FIND on 14, knob p on CC 11 + p with its press on CC 43 + p");
        {
            // its twelfth page is the snapshot knobs, knob for knob what the first scene's twelfth is
            const auto first = juce::JSON::parse (e16::sceneText (eFx, fxNames));
            const auto a = first["pages"][11], b = lib["pages"][11];
            bool same = a["title"].toString() == "SNAP" && b["title"].toString() == "SNAP" && (int) b["channel"] == e16::kSnapChannel && b["encoders"].size() == 16;
            for (int p = 0; p < 16 && same; ++p)
                same = juce::JSON::toString (a["encoders"][p], true) == juce::JSON::toString (b["encoders"][p], true)
                       && (int) b["encoders"][p]["turn_actions"][0]["channel"] == e16::kSnapChannel && (int) b["encoders"][p]["turn_actions"][0]["nr1"] == e16::kKnobCc + p
                       && (p == 0 || (int) b["encoders"][p]["push_action"]["nr1"] == (p == 12 ? (int) e16::kWheelKey : e16::kPressCc + p));
            // (knob 1 pressed is GLIDE and knob 13 pressed the wheel, as on a lane's page)
            same = same && (int) b["encoders"][0]["turn_actions"][1]["nr1"] == e16::kGlideCc && (int) b["encoders"][0]["turn_actions"][1]["channel"] == e16::kSnapChannel;
            check (same && b["encoders"][5]["name"].toString() == "OPEN UP" && b["encoders"][1]["name"].toString() == "SNAP 2",
                   "the second scene's twelfth page is the snapshot knobs too: the same sixteen, on the same addresses and with the same names, as the first scene's twelfth");
        }
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

    // 3c. the keys go down, each as its kind does
    {
        view (1);
        const int mute = cx::lane (2, cx::kMute);
        const auto mk = find ([mute] (const Screen::TestHit& h) { return Screen::testIsToggle (h, mute); });
        const auto pk = find ([] (const Screen::TestHit& h) { return Screen::testIsPanic (h); });
        auto near = [] (float a, float b) { return std::abs (a - b) < 0.01f; };
        const float base = st->heightAt (mk.x, mk.y) - Parts::kKey;
        const bool up0 = near (st->keyTopNow (mk.x, mk.y), Parts::kKey) && near (st->keyTopNow (pk.x, pk.y), Parts::kKey);
        // a latching key, under the hand: all the way in while the hand is on it, low once it lifts, and up again at the next press
        const auto p = pt (mk);
        sc.mouseDown (ev (p, p, false)); audio (4); fresh();
        const float held = st->keyTopNow (mk.x, mk.y);
        sc.mouseUp (ev (p, p, false)); fresh();
        const float on = st->keyTopNow (mk.x, mk.y), onTop = st->heightAt (mk.x, mk.y);
        const bool muted = ses.value (mute) > 0.5f;
        { const auto p2 = pt (mk); sc.mouseDown (ev (p2, p2, false)); audio (4); sc.mouseUp (ev (p2, p2, false)); fresh(); }
        const float off = st->keyTopNow (mk.x, mk.y);
        check (up0 && near (held, Parts::kKeyDown) && near (on, Parts::kKeyOn) && near (onTop, base + Parts::kKeyOn) && muted && near (off, Parts::kKey) && ses.value (mute) < 0.5f,
               "a latching key (a lane's MUTE) is all the way in under the hand, sits low while it is on, and comes up when it is pressed again (" + juce::String (held / Parts::kU, 2) + ", "
                   + juce::String (on / Parts::kU, 2) + ", " + juce::String (off / Parts::kU, 2) + " mm above its plate; up is " + juce::String (Parts::kKey / Parts::kU, 2) + ")");
        // a momentary key: in while the hand is on it, and back up
        const auto q = pt (pk);
        sc.mouseDown (ev (q, q, false)); fresh();
        const float in = st->keyTopNow (pk.x, pk.y);
        sc.mouseUp (ev (q, q, false)); fresh();
        check (near (in, Parts::kKeyDown) && near (st->keyTopNow (pk.x, pk.y), Parts::kKey), "a momentary key (PANIC) is in while the hand is on it and springs back (" + juce::String (in / Parts::kU, 2) + " mm, then " + juce::String (st->keyTopNow (pk.x, pk.y) / Parts::kU, 2) + ")");
        // latched from somewhere else (the E16's key, a snapshot): it travels there by way of the bottom, over a few frames, and nothing else is built
        fresh();
        // (its first frame is never more than a sixtieth of a second's travel, so the bottom is two frames off at the least, and the way back up one more)
        const int bottoms0 = st->keyBottoms(), builds0 = st->builds(), frames0 = st->keyFrames();
        fromOutside (mute, 1.0f, 1);
        sc.testTick(); sc.refreshNow();
        int waited = 0;
        for (; waited < 80 && ! near (st->keyTopNow (mk.x, mk.y), Parts::kKeyOn); ++waited) st->capture();
        const int builds1 = st->builds(), frames1 = st->keyFrames() - frames0;
        const bool wentDown = st->keyBottoms() == bottoms0 + 1;
        fromOutside (mute, 0.0f, 1);
        sc.testTick(); sc.refreshNow();
        int waited2 = 0;
        for (; waited2 < 80 && ! near (st->keyTopNow (mk.x, mk.y), Parts::kKey); ++waited2) st->capture();
        const int frames2 = st->keyFrames() - frames0 - frames1;
        check (waited < 80 && frames1 >= 3 && wentDown && builds1 == builds0 + 1 && st->keyBottoms() == bottoms0 + 2 && waited2 < 80 && frames2 >= 3,
               "latched from outside (the E16's key, a snapshot), a key travels there by way of the bottom, as the switch must: over " + juce::String (frames1) + " frames, with "
                   + juce::String (builds1 - builds0) + " build for the change and none for the travel; and the same way back up (" + juce::String (frames2) + " frames)");
        // a key on the keyboard: the action's key is seen to go down, and comes back by itself
        const auto rk = find ([] (const Screen::TestHit& h) { return Screen::testIsRun (h); });
        const bool ran = e.isRunning();
        sc.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
        fresh();
        const float tapped = st->keyTopNow (rk.x, rk.y);
        pump (200); fresh();
        const float after = st->keyTopNow (rk.x, rk.y);
        sc.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
        pump (200); fresh();
        check (near (tapped, Parts::kKeyDown) && near (after, ran ? Parts::kKey : Parts::kKeyOn) && near (st->keyTopNow (rk.x, rk.y), ran ? Parts::kKeyOn : Parts::kKey) && e.isRunning() == ran,
               "the space bar is seen on RUN: its key goes in, then stands where its state says (" + juce::String (tapped / Parts::kU, 2) + ", " + juce::String (after / Parts::kU, 2) + ", " + juce::String (st->keyTopNow (rk.x, rk.y) / Parts::kU, 2) + " mm)");
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
        // MOTION on a snapshot knob that moves knobs in three lanes: their lights go with the wave, and the eye stays home
        {
            const auto waves = Engine::motionWaves();
            ses.knobs[14].links = { { cx::lane (0, 2), 0.4f }, { cx::lane (1, 3), 0.4f }, { cx::lane (2, 5), 0.4f }, { cx::lane (3, 1), 0.4f } };
            ses.userSet (cx::kSnap + 14, 0.5f); audio (4); sc.testTick(); restFor (2600); sc.testTick();
            ses.userSet (cx::mot (2, 2), 14.0f / 15.0f); ses.userSet (cx::mot (2, 1), 2.0f / 20.0f);
            ses.userSet (cx::mot (2, 0), (float) waves.indexOf ("SINE") / (float) (waves.size() - 1)); ses.userSet (cx::mot (2, 3), 1.0f);
            bool eyeStayed = true; float lo = 9.0f, hi = -9.0f;
            for (int i = 0; i < 12; ++i)
            {
                audio (6); sc.testTick();
                eyeStayed = eyeStayed && sc.testFollowing().isEmpty();
                lo = std::min (lo, e.shown (cx::lane (1, 3))); hi = std::max (hi, e.shown (cx::lane (1, 3)));
            }
            ses.userSet (cx::mot (2, 0), 0.0f); ses.userSet (cx::mot (2, 3), 0.5f); ses.userSet (cx::mot (2, 2), 0.0f); ses.userSet (cx::mot (2, 1), e.control (cx::mot (2, 1)).def);
            ses.userSet (cx::kSnap + 14, 0.0f); ses.knobs[14].links.clear(); ses.turnSnapKnob (14, 0.0f);
            audio (6); sc.testTick(); restFor (2600); sc.testTick();
            check (eyeStayed && hi - lo > 0.1f && sc.testFollowing().isEmpty(), "MOTION swinging a snapshot knob that moves four lanes' knobs moves their lights (" + juce::String (hi - lo, 2) + " of a turn) and no eye");
        }
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

        // 5b. something chosen from the keyboard or a controller: the eye goes to it, and stays until something else is chosen
        using S = Session;
        ses.bringStop ({ S::fLane, 3, 0 }, 1); fresh(); sc.testTick();  // (TAB, or the wheel's second press: CLAP)
        const auto fb = sc.testFollowing();
        const bool toModule = ! fb.isEmpty() && fb.contains (laneRect (3)) && fb.getWidth() < laneRect (3).getWidth() + 40.0f;
        fresh();
        const bool away = st->isAway();
        restFor (2600);
        check (toModule && away && sc.testEyeHeld() && sc.testFollowing() == fb, "a module chosen by a key or a controller: the eye goes to it (" + fb.toString() + ") and stays there, left alone or not");
        ses.bringStop ({ S::fLane, 8, 0 }, 0); fresh(); sc.testTick(); sc.testTick();     // (the mouse, while the eye is held: this is what it holds now)
        const auto mb = sc.testFollowing();
        const bool movedOn = ses.activeLane == 8 && mb.contains (laneRect (8)) && ! mb.contains (laneRect (3));
        ses.point ({ S::fReturns, 0, 0 }); fresh(); sc.testTick();      // the pointer taken up: the eye goes home, to show every module
        const bool homeForIt = sc.testFollowing().isEmpty() && ! st->isAway() && ses.front.kind == S::fLane;
        ses.point ({ S::fMaster, 0, 0 }); fresh(); sc.testTick();
        const bool homeStill = sc.testFollowing().isEmpty();
        ses.pointOff(); fresh(); sc.testTick();                         // put down with nothing chosen: back to what it held
        const bool backTo = sc.testFollowing() == mb && sc.testEyeHeld();
        check (movedOn && homeForIt && homeStill && backTo, "another brought to the front with the mouse while the eye is held is what it holds (" + mb.toString() + "); with the pointer up the eye is home, and put down with nothing chosen it is back where it was");
        ses.configOfFront (1);                                          // CONFIG, from the keyboard: the eye holds the panel and the module beside it
        restFor (700);
        const auto cb = sc.testFollowing();
        const bool holdsBoth = ! cb.isEmpty() && cb.contains (sc.testConfigRect().reduced (12.0f)) && cb.contains (sc.testStopRect ({ S::fLane, 8, 0 }).reduced (12.0f));
        restFor (2600);
        const bool heldOpen = sc.testFollowing() == cb;
        ses.configOfFront (1);
        restFor (700);
        const auto ab = sc.testFollowing();
        const bool toModuleAgain = ! ses.cfgOpen && ab.contains (laneRect (8)) && ab.getWidth() * ab.getHeight() < cb.getWidth() * cb.getHeight();
        check (holdsBoth && heldOpen && toModuleAgain, "CONFIG opened from the keyboard or a controller: the eye holds its panel and the module beside it (" + cb.toString() + ") for as long as it is open, and the module alone when it is shut");
        sc.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); fresh(); sc.testTick();
        const bool letGo = ! sc.testEyeHeld() && sc.testFollowing().isEmpty();
        ses.bringStop ({ S::fLane, 2, 0 }, 0); fresh(); sc.testTick(); sc.testTick();     // (the mouse, with the eye home: the hand is already there)
        check (letGo && ses.activeLane == 2 && sc.testFollowing().isEmpty() && ! sc.testEyeHeld(), "Esc lets the eye go home; a module brought to the front with the mouse from there does not move it");
        ses.bringLane (7, 0); fresh();
    }
    // 5c. LEARN: a knob moved while a snapshot knob is being taught is lit yellow (moved from outside too, with no hand on the screen), and KEEP is reached in the top bar
    {
        const int ctl = cx::lane (0, 5);            // KICK PUNCH: a white ring
        const auto ku = find ([&] (const Screen::TestHit& h) { return Screen::testIsControl (h, ctl); });
        auto yellow = [&] (const juce::Image& img)
        {
            const auto c = pt (ku).roundToInt();
            int n = 0;
            for (int y = c.y - 26; y <= c.y + 26; ++y)
                for (int x = c.x - 26; x <= c.x + 26; ++x)
                    if (x >= 0 && y >= 0 && x < img.getWidth() && y < img.getHeight())
                    { const auto p = img.getPixelAt (x, y); if (p.getRed() > 170 && p.getGreen() > 140 && p.getBlue() < 110 && (int) p.getRed() - (int) p.getBlue() > 90) ++n; }
            return n;
        };
        const float was = ses.value (ctl);
        const int before = yellow (fresh());
        ses.learnBegin (11);
        fromOutside (ctl, was > 0.5f ? was - 0.35f : was + 0.35f, 2); sc.testTick(); sc.testTick();
        const int during = yellow (fresh());
        const auto keep = find ([] (const Screen::TestHit& h) { return Screen::testIsLearnKey (h, 1); });
        click (keep);
        const int after = yellow (fresh());
        const bool tied = keep.x > 0.0f && ses.learning < 0 && ses.knobs[11].links.size() == 1 && ses.knobs[11].links[0].ctl == ctl;
        ses.turnSnapKnob (11, 0.0f); ses.knobs[11].links.clear(); fresh();
        check (before < 4 && during >= before + 8 && tied && after < 4 && std::abs (ses.value (ctl) - was) < 1e-4f,
               "LEARN: a knob moved while a snapshot knob is being taught is lit yellow (" + juce::String (before) + " yellow points round it before, " + juce::String (during) + " while, " + juce::String (after)
                   + " after); KEEP in the top bar ties it, and with the knob turned back it is where it was");
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

        // 7b. the dark instrument's wear: a sheet made from a photograph of scratched metal, a different window of it on every plate
        {
            auto rowsApart = [] (const juce::Image& x, const juce::Image& y)
            {
                if (! x.isValid() || x.getBounds() != y.getBounds()) return -1;
                const juce::Image::BitmapData a (x, juce::Image::BitmapData::readOnly), c (y, juce::Image::BitmapData::readOnly);
                int n = 0;
                for (int yy = 0; yy < a.height; ++yy) if (std::memcmp (a.getLinePointer (yy), c.getLinePointer (yy), (size_t) a.width * 4) != 0) ++n;
                return n;
            };
            check (st->wearReady(), "the sheet of scratches is read and reaches the card");
            st->showWear (false);
            const auto bare = fresh();
            st->showWear (true);
            const int darkRows = rowsApart (dark, bare), againRows = rowsApart (dark, fresh());
            sc.setDark (false);
            const auto pale = fresh();
            st->showWear (false);
            const int paleRows = rowsApart (pale, fresh());
            st->showWear (true);
            sc.setDark (true); fresh();
            check (darkRows > 200 && againRows == 0 && paleRows == 0, "it shows on the dark instrument (" + juce::String (darkRows) + " rows of the picture are different without it; put back, " + juce::String (againRows)
                       + ") and the pale one is as it was (" + juce::String (paleRows) + ")");

            // every plate has its own number, which says which of the wear it takes; it goes by where the plate is drawn, so a plate that slides keeps it
            auto lanePlates = [&]
            {
                std::vector<std::pair<float, float>> v;      // (number, where it stands)
                for (const auto& p : sc.testParts().plates) if (std::abs (p.r.getWidth() - 272.0f) < 0.5f && std::abs (p.r.getHeight() - 344.0f) < 0.5f) v.push_back ({ p.seed, p.r.getX() + p.r.getY() * 10000.0f });
                return v;
            };
            const auto shut = lanePlates();
            std::set<float> numbers;
            bool inRange = true;
            for (const auto& p : shut) { numbers.insert (p.first); inRange = inRange && p.first >= 0.0f && p.first < 0.98f; }
            sc.showConfig ({ 0, 0, 0 }); fresh();
            const auto open = lanePlates();
            int known = 0, moved = 0;
            for (const auto& p : open)
            {
                if (numbers.count (p.first) != 0) ++known;
                for (const auto& q : shut) if (q.first == p.first && q.second != p.second) { ++moved; break; }
            }
            sc.hideConfig(); fresh();
            check (shut.size() == 12 && numbers.size() == 12 && inRange && ! open.empty() && known == (int) open.size() && moved >= 6,
                   "each plate has its own wear (" + juce::String ((int) numbers.size()) + " numbers for the " + juce::String ((int) shut.size()) + " lane-sized plates) and keeps it when CONFIG slides the rows over ("
                       + juce::String (moved) + " plates moved, " + juce::String (known) + " of " + juce::String ((int) open.size()) + " with the number they had)");
        }
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
        for (auto* l : { &load, &ins, &e16, &delay, &clockLabel, &status, &stepsLabel, &late, &knobLabel, &pagesLabel, &slotsLabel })
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
        // the E16's pages turned from here: the channel it listens on for Program Change, and the slots its two scenes are in
        pagesLabel.setText ("E16 PAGES", juce::dontSendNotification);
        slotsLabel.setText ("PC CHANNEL, THEN THE SLOTS OF SF-000 AND SF-FX", juce::dontSendNotification);
        slotsLabel.setFont (juce::FontOptions (12.0f));
        for (auto* c : { &pcBox, &slotA, &slotB })
        {
            addAndMakeVisible (*c);
            c->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff4D514E));
            c->setColour (juce::ComboBox::textColourId, juce::Colour (0xffE8E5E1));
        }
        pcBox.addItem ("OFF", 1);
        for (int ch = 1; ch <= 16; ++ch) { pcBox.addItem (juce::String (ch), ch + 1); slotA.addItem (juce::String (ch), ch); slotB.addItem (juce::String (ch), ch); }
        pcBox.setSelectedId (midi.pcChannel() + 1, juce::dontSendNotification);
        slotA.setSelectedId (midi.sceneSlot (0), juce::dontSendNotification);
        slotB.setSelectedId (midi.sceneSlot (1), juce::dontSendNotification);
        pcBox.onChange = [this] { midi.setPcChannel (pcBox.getSelectedId() - 1); };
        slotA.onChange = [this] { midi.setSceneSlot (0, slotA.getSelectedId()); };
        slotB.onChange = [this] { midi.setSceneSlot (1, slotB.getSelectedId()); };
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
        // ---- files: where the library is, and where takes go
        for (auto* l : { &libLabel, &libPath, &recLabel, &recPath, &filesNote })
        {
            addAndMakeVisible (*l);
            l->setColour (juce::Label::textColourId, juce::Colour (0xffE8E5E1));
            l->setFont (juce::FontOptions (l == &filesNote ? 12.0f : 14.0f));
            l->setMinimumHorizontalScale (0.6f);
        }
        filesNote.setJustificationType (juce::Justification::topLeft);
        libLabel.setText ("LIBRARY", juce::dontSendNotification);
        recLabel.setText ("TAKES", juce::dontSendNotification);
        for (auto* b : { &libChoose, &recChoose })
        {
            addAndMakeVisible (*b);
            b->setButtonText ("CHOOSE");
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4D514E));
            b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffE8E5E1));
        }
        libChoose.onClick = [this] { choose (true); };
        recChoose.onClick = [this] { choose (false); };
        showFiles();
        setSize (520, 882 + 98);
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
        {
            auto pr = r.removeFromTop (26);
            pagesLabel.setBounds (pr.removeFromLeft (110));
            pcBox.setBounds (pr.removeFromLeft (64)); pr.removeFromLeft (6);
            slotA.setBounds (pr.removeFromLeft (56)); pr.removeFromLeft (6);
            slotB.setBounds (pr.removeFromLeft (56)); pr.removeFromLeft (8);
            slotsLabel.setBounds (pr);
        }
        r.removeFromTop (6);
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
        {
            // FILES, at the foot: the library's folder, the takes' folder, and a line about them
            auto files = r.removeFromBottom (98);
            files.removeFromTop (8);
            auto row1 = files.removeFromTop (26);
            libLabel.setBounds (row1.removeFromLeft (110));
            libChoose.setBounds (row1.removeFromRight (86)); row1.removeFromRight (6);
            libPath.setBounds (row1);
            files.removeFromTop (4);
            auto row2 = files.removeFromTop (26);
            recLabel.setBounds (row2.removeFromLeft (110));
            recChoose.setBounds (row2.removeFromRight (86)); row2.removeFromRight (6);
            recPath.setBounds (row2);
            filesNote.setBounds (files);
        }
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
        e16.setText ("E16: " + (midi.e16Name().isNotEmpty() ? midi.e16Name() : juce::String ("NOT FOUND")) + (midi.pageNote().isNotEmpty() ? "   (" + midi.pageNote() + ")" : juce::String()), juce::dontSendNotification);
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
    juce::Label load, ins, e16, delay, clockLabel, status, stepsLabel, late, knobLabel, pagesLabel, slotsLabel;
    juce::ComboBox pcBox, slotA, slotB;
    juce::TextButton off, send, follow, onBeat, withNotes;
    juce::Slider lead, trim;
    juce::Label lookLabel, glowLabel, hueLabel, brightLabel, pulseLabel;
    juce::Slider glowHue, glowBright, glowPulse;
    juce::TextButton fullScreen;
    // FILES: the library's folder and the folder takes go to
    juce::Label libLabel, libPath, recLabel, recPath, filesNote;
    juce::TextButton libChoose, recChoose;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String libSaid;
    void showFiles()
    {
        auto spoken = [] (const juce::File& f)
        {
            const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            return f.isAChildOf (home) ? "~/" + f.getRelativePathFrom (home) : f.getFullPathName();
        };
        const auto& p = sf0::Session::places();
        libPath.setText (spoken (sf0::Session::libraryDir()), juce::dontSendNotification);
        const juce::File rec = sf0::Session::recordingsDir();
        recPath.setText (spoken (rec) + (rec.isDirectory() || rec == sf0::Session::usualRecordingsDir() ? "" : "   (NOT THERE NOW)"), juce::dontSendNotification);
        juce::String note = libSaid;
        if (note.isEmpty() && p.next.isNotEmpty()) note = "FROM THE NEXT START THE LIBRARY IS IN " + (p.next == "(home)" ? spoken (sf0::Session::homeDir()) : p.next).toUpperCase() + ". QUIT AND OPEN SF-000 AGAIN.";
        if (note.isEmpty() && p.libraryMissing) note = "THE LIBRARY FOLDER CHOSEN IS NOT THERE (" + p.library.toUpperCase() + "): USING THE ONE ON THIS COMPUTER THIS TIME.";
        if (note.isEmpty()) note = "THE LIBRARY IS THE SESSION, THE SNAPSHOTS' CLIPS AND A COPY OF EVERY SAMPLE: ALL SF-000 NEEDS ON ANOTHER COMPUTER.";
        filesNote.setText (note, juce::dontSendNotification);
    }
    void choose (bool library)
    {
        chooser = std::make_unique<juce::FileChooser> (library ? "The folder for SF-000's library" : "The folder SF-000's takes go to",
                                                       library ? sf0::Session::libraryDir() : sf0::Session::recordingsDir());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, library] (const juce::FileChooser& fc)
        {
            const juce::File f = fc.getResult();
            if (f == juce::File()) return;
            if (library) libSaid = sf0::Session::chooseLibrary (f);
            else { libSaid = sf0::Session::chooseRecordings (f); if (onRecordings) onRecordings(); }
            showFiles();
        });
    }
public:
    std::function<void()> onRecordings;         // the folder for takes was chosen
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

        sf0::Session::settlePlaces();        // where the library is this time (a folder chosen in SETUP is taken up here)
        engine = std::make_unique<sf0::Engine>();
        session = std::make_unique<sf0::Session> (*engine);
        std::unique_ptr<juce::XmlElement> saved (juce::XmlDocument::parse (audioFile()));
        const auto err = devices.initialise (0, 4, saved.get(), true);       // the set on 1 + 2, the headphones' cue on 3 + 4
        if (err.isNotEmpty()) juce::Logger::writeToLog ("SF-000 audio: " + err);
        devices.addAudioCallback (engine.get());
        midi = std::make_unique<sf0::Midi> (*engine, *session);
        recorder = std::make_unique<sf0::Recorder> (*engine);
        recorder->root = sf0::Session::recordingsDir();
        recorder->stateAtStart = [this] { session->save(); return sf0::Session::stateFile(); };
        // a print renders on a rig of its own: this one's audio waits meanwhile (one rig sounds at a time)
        session->audioPause = [this] { devices.removeAudioCallback (engine.get()); };
        session->audioResume = [this] { devices.addAudioCallback (engine.get()); };
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
        // the library's folder: moved, or not found this time
        if (const auto& p = sf0::Session::places(); p.note.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (p.libraryMissing ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                                                    p.note.upToFirstOccurrenceOf ("\n", false, false), p.note.fromFirstOccurrenceOf ("\n\n", false, false));
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
    // (this machine's own: with its settings, not in the library folder)
    static juce::File audioFile() { return sf0::Session::homeFile ("audio.xml"); }
    static juce::File lookFile() { return sf0::Session::homeFile ("look.json"); }

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
        sel->onRecordings = [this] { if (recorder != nullptr) recorder->root = sf0::Session::recordingsDir(); };       // (from the next take)
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
