// CONTROL, ALLOC, TAP
#include "Kit.h"
#include "../core/E16Glide.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <cstdio>
#include <map>
#include <mutex>

namespace sf
{
// =====================================================================================  CONTROL
// Suite brain: six macros, song select (Program Change on the chosen channel, default 16),
// STORE ALL, KEY (every pitched SF instrument follows) and PANIC (every SF plugin silences its
// voices, tails and delay lines). Put it on its own MIDI track receiving the OXI One.
// STORE TO: the song STORE ALL writes into, chosen without recalling it (choosing SONG recalls
// a stored song, which would replace the sound before it could be stored). That song becomes
// the one in force (unless a song change is waiting for its bar), and STORE TO goes back to
// ACTIVE SONG.
// SLOW ALL: how long your next knob moves take to arrive, in every SF plugin whose own SLOW is
// CONTROL (the default). OFF = straight there. A move keeps the length it was made with, so
// turning SLOW ALL never touches a knob that is already travelling. Song recalls follow it too.
// E16 RINGS: the E16 USB port SF CONTROL tells the E16 scenes' script about travelling knobs
// on (their rings turn green and follow the sound). OFF: nothing is sent. A port that isn't there
// is looked for again every 2 s.
// NEXT SONG and GO [Nov 8]: the song call on the E16 (knob 15 of every PERF page). Turning picks NEXT SONG and
// changes nothing else, so a knob knocked in passing never changes the song; GO (its press) asks for
// that song, on the next bar line as a turn of SONG would (Live stopped: at once). GO on the song
// already asked for does nothing. NEXT SONG follows the song whenever it changes, by any way, so the
// knob and its ring show the song in force until it is turned. Live sends no Program Change to a
// VST3: this is how a set of VST3s calls its songs from the stage.
class ControlModule : public Module, private juce::Timer
{
public:
    enum { M1, M2, M3, M4, M5, M6, SONG, STOREALL, PCCH, PCON, POFFSET, KEY, PANIC, STORETO, SLOWALL, E16RINGS, NEXTSONG, SONGGO };
    ControlModule() { startTimerHz (20); }
    ~ControlModule() override { stopTimer(); }
    std::string id() const override { return "CONTROL"; }
    bool instrument() const override { return true; }
    bool takesDropCut (const Ctx&) const override { return false; }
    std::vector<ParamSpec> params() const override
    {
        std::vector<ParamSpec> p;
        for (int i = 1; i <= kMacros; ++i)
        {
            auto s = pct (("macro" + std::to_string (i)).c_str(), ("MACRO " + std::to_string (i)).c_str(), 0.0f);
            s.primary = true; s.song = true;
            p.push_back (s);
        }
        std::vector<std::string> songs; for (int i = 1; i <= kSongs; ++i) songs.push_back ("SONG " + std::string (i < 10 ? "0" : "") + std::to_string (i));
        auto sg = choice ("song", "SONG", songs, 0); sg.primary = true; sg.song = false; p.push_back (sg);
        p.push_back (momentary ("store_all", "STORE ALL").prim());
        std::vector<std::string> ch; for (int i = 1; i <= 16; ++i) ch.push_back ("CH " + std::to_string (i));
        auto pc = choice ("pc_channel", "PC CHANNEL", ch, 15); pc.song = false; pc.setup = true; p.push_back (pc);
        auto on = toggle ("pc_enable", "PC ENABLE", true); on.song = false; on.setup = true; p.push_back (on);
        // Calibration for Pocket timing in Live (+ = every pocket later). Set once, by ear.
        auto off = knob ("pocket_offset", "POCKET OFFSET", -20.0f, 20.0f, 0.0f, "ms"); off.song = false; off.glide = false; off.setup = true; p.push_back (off);
        // KEY: parts written in C play in this key (each song can store its own).
        p.push_back (choice ("key", "KEY", { "OFF", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
        p.push_back (momentary ("panic", "PANIC").performs());
        std::vector<std::string> to { "ACTIVE SONG" }; for (auto& sn : songs) to.push_back (sn);
        auto st = choice ("store_to", "STORE TO", to, 0); st.song = false; st.preset = false; st.setup = true; p.push_back (st);
        // A performance setting, like a hand on a knob: songs and presets leave it alone.
        auto sa = choice ("slow_all", "SLOW ALL", { "OFF", "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR", "32 BAR", "64 BAR", "128 BAR" }, 0);
        sa.song = false; sa.preset = false; p.push_back (sa);
        auto er = choice ("e16_rings", "E16 RINGS", { "OFF", "PORT 1", "PORT 2", "PORT 3" }, 3);
        er.song = false; er.preset = false; er.setup = true; p.push_back (er);
        // Played, like SLOW ALL: songs and presets leave them alone.
        auto nx = choice ("next_song", "NEXT SONG", songs, 0); nx.song = false; nx.preset = false; nx.perform = true; p.push_back (nx);
        auto go = momentary ("song_go", "GO"); go.preset = false; go.perform = true; p.push_back (go);
        return p;
    }
    // VST3 delivers Program Change as a program change: 16 programs = 16 songs.
    int numPrograms() const override { return kSongs; }
    int currentProgram() const override { return std::max (0, (int) Bus::get().d().song.load()); }
    // (Host thread: published, and SONG then follows, from the next audio block, which knows the
    // song position.)
    void setProgram (int p) override
    {
        const int s = juce::jlimit (0, kSongs - 1, p);
        // The newest request replaces a waiting one. The song already in force cancels a waiting
        // change and recalls nothing (recalling it again would undo the live edits): SONG 02 then
        // SONG 01 from SONG 01, before the next block, used to land SONG 02.
        if (Bus::get().d().song.load() != s) programRequest.store (s);
        else { programRequest.store (-1); pcSong = s; }
    }
    void prepare (double, int) override {}
    void process (Ctx& c, juce::AudioBuffer<float>&, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midi) override
    {
        auto& d = c.bus->d();
        d.controlSeenMs = Bus::nowMs();
        d.pocketOffsetMs = c.v (POFFSET);
        d.keyRoot = c.ci (KEY) - 1;
        // SLOW ALL: a change counts from the next host block in every plugin (Bus.h), so a move or
        // a recall made in the same block takes the same length everywhere. (Each plugin read the
        // length at its own block start: those Live processed before SF CONTROL took the old one,
        // those after it the new one, for the whole move.) The fence is for that one host block:
        // cleared in the next (the change is published in a block's last chunk, so any later call
        // is a later block), or an Arrangement loop or a relocate back to the song position of the
        // change would give a new move the old length again.
        if (slowFenceUp)
        {
            slowFenceUp = false;
            d.slowSeq.fetch_add (1);
            d.slowAllFromPpq = -1.0;
            d.slowSeq.fetch_add (1);
        }
        if (c.lastChunk)
        {
            const int sa = juce::jlimit (0, kSlowAllTop, c.ci (SLOWALL));
            if (sa != d.slowAll.load())
            {
                d.slowSeq.fetch_add (1);                                     // odd: being written
                d.slowAllPrev = d.slowAll.load();
                d.slowAllFromPpq = c.playing ? c.ppqAt (c.n) : -1.0;
                d.slowAll = sa;
                d.slowSeq.fetch_add (1);
                slowFenceUp = c.playing;
            }
        }
        const bool pn = c.b (PANIC);
        if (pn && ! panicWas) d.panicSeq.fetch_add (1);
        d.panicHeld = pn ? 1 : 0;
        panicWas = pn;
        // Song changes carry the bar line they land on, decided here once for every plugin, so
        // the order Live processes the tracks in can't split a transition across two bars, and
        // where they were asked for (a plugin that sees one just after a loop wrapped can tell
        // whether it came before or after the wrap).
        bool published = false;
        auto publishSong = [&] (int s, int offset, bool fromMidi = false)
        {
            // The key the song brings switches on the same bar in every plugin (-2: no change).
            const float k = getSongValue ? getSongValue (s, KEY) : NAN;
            d.songKey = std::isnan (k) ? -2 : (int) std::lround (k) - 1;
            d.songFromPpq = c.ppqAt (offset);
            d.songAtPpq = songTarget (c, c.ppqAt (offset), fromMidi ? kMidiWindowBeats : kHandWindowSec * c.bpm / 60.0);
            d.song = s;
            d.songSeq.fetch_add (1);
            published = true;
        };

        for (int i = 0; i < kMacros; ++i) d.macro[i] = c.v (M1 + i);
        // Program Change selects the song.
        if (c.b (PCON))
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isProgramChange() && m.getChannel() == c.ci (PCCH) + 1)
                {
                    const int s = juce::jlimit (0, kSongs - 1, m.getProgramChangeNumber());
                    publishSong (s, juce::jlimit (0, std::max (0, c.n - 1), meta.samplePosition), true);
                    pcSong = s;
                }
            }
        // A change of the SONG parameter (knob, menu, automation) selects that song. SONG
        // following the bus (the timer, after a Program Change or STORE TO) is no request: a
        // Program Change that arrived meanwhile is newer and stays.
        const int sp = c.ci (SONG);
        if (sp != lastSongParam)
        {
            lastSongParam = sp;
            songParamNow = sp;
            if (sp != mirroredSong.exchange (-1) && sp != d.song.load()) publishSong (sp, 0);
        }
        // GO: NEXT SONG, as asked for now (its press's leading edge); nothing if that song is the one
        // asked for already. SONG follows, as after a Program Change.
        const bool go = c.b (SONGGO);
        if (go && ! goWas)
            if (const int nx = c.ci (NEXTSONG); nx != d.song.load()) { publishSong (nx, 0); pcSong = nx; }
        goWas = go;
        // A host program change (VST3), last: it is the newest request.
        if (const int pr = programRequest.exchange (-1); pr >= 0) { publishSong (pr, 0); pcSong = pr; }

        // STORE ALL, into STORE TO's song or the song in force. It goes out at the end of a host
        // block in which no song recall landed: in such a block the plugins Live processes before
        // SF CONTROL would store after the recall, and those after it before.
        const bool st = c.b (STOREALL);
        if (st && ! storeWas)
        {
            storeHeld = true;
            storeTo = c.ci (STORETO) - 1;
            storeKeepSong = c.recallWaiting || published;     // a song change under way stays the song in force
        }
        storeWas = st;
        if (storeHeld && c.lastChunk)
        {
            if (c.recalledThisBlock) storeKeepSong = true;   // a song landed as you stored: it stays in force
            else
            {
                storeHeld = false;
                if (storeTo >= 0 && storeTo < kSongs)
                {
                    // STORE TO: the sound playing now goes into that song, which isn't recalled. It
                    // is the song in force from now (SONG follows), unless a song change is under way.
                    const bool keep = storeKeepSong || c.recallWaiting;
                    d.storeSong = storeTo;
                    if (! keep) d.song = storeTo;
                    storeMirror = keep ? kSongs : storeTo;
                }
                else
                {
                    d.storeSong = -1;
                    if (d.song.load() < 0) publishSong (sp, 0);
                }
                d.storeSeq.fetch_add (1);
            }
        }
    }
    // The suite at a glance, two columns (the macros are on the E16 grid above it).
    int displayHeight() const override { return 116; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto& bus = Bus::get();
        auto& d = bus.d();
        ui::frame (g, r, "SUITE");
        auto s = r.reduced (10, 18).withTrimmedTop (2);
        int n = 0;
        const float colW = s.getWidth() * 0.5f;
        auto line = [&] (const juce::String& k, const juce::String& v, juce::Colour col)
        {
            juce::Rectangle<float> row (s.getX() + colW * (float) (n % 2), s.getY() + 15.0f * (float) (n / 2), colW - 6.0f, 15.0f);
            ++n;
            ui::label (g, k, row.removeFromLeft (52), ui::kFaint, 9.0f);
            ui::label (g, v, row, col, 10.0f);
        };
        const int song = d.song.load();
        line ("SONG", song < 0 ? "--" : juce::String (song + 1).paddedLeft ('0', 2), ui::kText);
        // NEXT SONG: amber while it picks a song GO hasn't called yet.
        const int next = getParam ? juce::jlimit (0, kSongs - 1, (int) std::lround (getParam (NEXTSONG))) : -1;
        const bool picked = next >= 0 && next != song;
        line ("NEXT", next < 0 ? juce::String ("--") : juce::String (next + 1).paddedLeft ('0', 2) + (picked ? "  PRESS GO" : ""),
              picked ? ui::kAmber : ui::kFaint);
        static const char* const keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int key = d.keyRoot.load();
        line ("KEY", key >= 0 && key < 12 ? juce::String (keys[key]) : juce::String ("OFF"), key >= 0 ? ui::kText : ui::kFaint);
        line ("BUS", bus.shared() ? "SHARED" : "LOCAL ONLY", bus.shared() ? ui::kGreen : ui::kAmber);
        line ("KICK", bus.kickAlive (2000) ? "REPORTING" : "SILENT", bus.kickAlive (2000) ? ui::kGreen : ui::kFaint);
        line ("ALLOC", bus.allocAlive() ? "RUNNING" : "NOT LOADED", bus.allocAlive() ? ui::kGreen : ui::kFaint);
        const int want = getParam ? juce::jlimit (0, 3, (int) std::lround (getParam (E16RINGS))) : 0;
        const bool found = want != 0 && e16State.load() == 2 && e16Port.load() == want;
        line ("E16 RINGS", want == 0 ? juce::String ("OFF") : (found ? "PORT " : "NO PORT ") + juce::String (want), found ? ui::kGreen : ui::kFaint);
        int alive = 0; juce::String names;
        for (int i = 0; i < kSources; ++i)
            if (bus.sourceAlive (i))
            {
                ++alive;
                char nm[24]; Bus::unpackName (d.src[i].name, nm);
                if (names.length() < 90) names << nm << "  ";
            }
        line ("SOURCES", juce::String (alive), ui::kText);
        ui::lines (g, names.trim(), s.withTrimmedTop (15.0f * 4.0f + 2.0f), ui::kDim, 9.0f, 2);
    }
private:
    void timerCallback() override
    {
        // SONG follows the bus after a Program Change or STORE TO, so Live (and the E16) show the
        // song in force; only while it still is (a newer request since stays). STORE TO goes back
        // to ACTIVE SONG.
        const auto& d = Bus::get().d();
        if (const int pc = pcSong.exchange (-1); pc >= 0 && d.song.load() == pc) mirrorSong (pc);
        // NEXT SONG follows the song whenever it changes (GO, SONG, a Program Change, STORE TO, another
        // plugin's load): a pick not yet called gives way to it.
        if (const int now = d.song.load(); now != nextFollows)
        {
            nextFollows = now;
            if (now >= 0 && now < kSongs && setParam && getParam && (int) std::lround (getParam (NEXTSONG)) != now)
                setParam (NEXTSONG, (float) now);
        }
        if (const int s = storeMirror.exchange (-1); s >= 0 && setParam)
        {
            if (s < kSongs && d.song.load() == s) mirrorSong (s);
            setParam (STORETO, 0.0f);
        }
        updateE16();
    }

    // E16 RINGS (message thread, 20 Hz): find the port, then send what the plugins report.
    static bool isE16Port (const juce::String& name, int n)
    {
        const auto s = name.toUpperCase();
        const auto k = juce::String (n);
        return s.contains ("E16") && (s.endsWith (" " + k) || s.contains ("PORT " + k) || s.endsWith ("PORT" + k));
    }
    void updateE16()
    {
        const int want = getParam ? juce::jlimit (0, 3, (int) std::lround (getParam (E16RINGS))) : 0;
        if (want != e16Port.load()) { out.reset(); e16Port = want; e16Check = 0; glide.reset(); }
        if (want == 0) { e16State = 0; return; }
        if (--e16Check <= 0)
        {
            e16Check = 40;
            juce::MidiDeviceInfo found;
            for (const auto& dev : juce::MidiOutput::getAvailableDevices())
                if (isE16Port (dev.name, want)) { found = dev; break; }
            if (found.identifier.isEmpty()) { out.reset(); glide.reset(); }
            else if (out == nullptr || out->getIdentifier() != found.identifier)
            {
                out = juce::MidiOutput::openDevice (found.identifier);
                glide.reset();
            }
            e16State = out != nullptr ? 2 : 1;
        }
        if (out != nullptr)
            for (const auto& m : glide.collect (Bus::get(), Bus::nowMs()))
                out->sendMessageNow (juce::MidiMessage (m.data(), (int) m.size()));
    }
    std::unique_ptr<juce::MidiOutput> out;
    E16Glide glide;
    std::atomic<int> e16Port { -1 }, e16State { 0 };
    int e16Check = 0;
    // Marked, so the audio thread never takes it for a request.
    void mirrorSong (int s)
    {
        if (! setParam || s == songParamNow.load()) return;
        mirroredSong = s;
        setParam (SONG, (float) s);
    }
    std::atomic<int> pcSong { -1 }, programRequest { -1 }, storeMirror { -1 }, mirroredSong { -1 }, songParamNow { -1 };

    // The bar line a song change lands on. Stopped: at once (-1). Within `window` beats after a bar
    // line: that bar, and plugins that see it a block late catch up at once. Later in the bar: the
    // next bar line. A Program Change from MIDI: a sixteenth. A sequencer sends it on its downbeat,
    // and one clocked by Live plays after Live's [Nov 7]: the OXI One's notes came 40 ms after
    // Live's beat, past the 30 ms this window was, so every song change from it landed a bar late.
    // The SONG knob, its menu, automation, a host program: 30 ms (a hand a little late on the one
    // waits for the next bar rather than landing in the middle of this one).
    static constexpr double kMidiWindowBeats = 0.25, kHandWindowSec = 0.030;
    static double songTarget (const Ctx& c, double reqPpq, double windowBeats)
    {
        if (! c.playing) return -1.0;
        const double bpb = c.beatsPerBar;
        double since = std::fmod (reqPpq - c.barPhase, bpb);
        if (since < 0.0) since += bpb;
        const double barStart = reqPpq - since;
        return since < windowBeats ? barStart : barStart + bpb;
    }
    int lastSongParam = -1, storeTo = -1;
    bool storeWas = false, panicWas = false, storeHeld = false, storeKeepSong = false, goWas = false;
    int nextFollows = -2;                 // the bus song NEXT SONG last followed (message thread)
    bool slowFenceUp = false;              // a SLOW ALL change's fence is on the bus: cleared next block
};
ModulePtr makeCONTROL() { return std::make_unique<ControlModule>(); }

// =====================================================================================  ALLOC
// Mix assigner. Sources are put in order of importance (drag in the list, top = most important):
// one global order, and any band can have its own order instead (click the band, drag; LINK
// returns it to the global order). Each of the 10 bands has a priority curve over its order:
//   PRIORITY - where the curve's peak sits (top .. bottom of the order)
//   DEPTH    - how hard sources away from the peak are cut (0 = nothing cut)
//   Q        - width of the peak (wide = neighbours share, narrow = one source owns the band)
// MODE DYNAMIC cuts a source only while a more favoured source is playing in that band;
// STATIC cuts all the time (a fixed EQ). Pass-through audio: put it on the master.
class AllocModule : public Module, private juce::Timer
{
public:
    enum { ENABLE, MODE, MAXCUT, SENS, PRI0 = 4, DEPTH0 = PRI0 + kBands, Q0 = DEPTH0 + kBands };
    AllocModule()
    {
        for (auto& band : rank) for (auto& r : band) r = -1;
        for (auto& l : linked) l = true;
        startTimerHz (20);
    }
    ~AllocModule() override { stopTimer(); }
    std::string id() const override { return "ALLOC"; }
    std::vector<ParamSpec> params() const override
    {
        std::vector<ParamSpec> p {
            toggle ("enable", "ENABLE", true).prim(),
            choice ("mode", "MODE", { "DYNAMIC", "STATIC" }, 0).prim(),
            knob ("max_cut", "MAX CUT", 0.0f, 24.0f, 12.0f, "dB").prim(),
            pct ("sensitivity", "SENSITIVITY", 0.5f).prim() };
        for (int b = 0; b < kBands; ++b) p.push_back (pct (("pri" + std::to_string (b + 1)).c_str(), ("PRIORITY " + std::to_string (b + 1)).c_str(), 0.0f).hide());
        for (int b = 0; b < kBands; ++b) p.push_back (pct (("depth" + std::to_string (b + 1)).c_str(), ("DEPTH " + std::to_string (b + 1)).c_str(), 0.0f).hide());
        for (int b = 0; b < kBands; ++b) p.push_back (pct (("q" + std::to_string (b + 1)).c_str(), ("Q " + std::to_string (b + 1)).c_str(), 0.5f).hide());
        return p;
    }
    void prepare (double rate, int) override { sr = rate; }

    static void curve (float pri, float q, int n, float& peak, float& sigma)
    {
        peak = pri * (float) std::max (0, n - 1);
        sigma = lerp (3.0f, 0.3f, q);
    }
    static float weight (int i, float peak, float sigma) { const float d = ((float) i - peak) / sigma; return std::exp (-0.5f * d * d); }

    void process (Ctx& c, juce::AudioBuffer<float>&, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        auto& d = c.bus->d();
        d.allocSeenMs = Bus::nowMs();
        const bool en = c.b (ENABLE), dynamic = c.ci (MODE) == 0;
        const float maxCut = c.v (MAXCUT);
        const float floorDb = -58.0f + (1.0f - c.v (SENS)) * 22.0f;
        bool alive[kSources];
        for (int s = 0; s < kSources; ++s) alive[s] = Bus::get().sourceAlive (s) && d.src[s].enabled.load() != 0;
        for (int s = 0; s < kSources; ++s) for (int b = 0; b < kBands; ++b) cutOut[s][b] = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            // This band's order (ranks come from the message thread); unplaced sources after it, by role.
            int order[kSources], n = 0, slotAt[kSources];
            for (auto& x : slotAt) x = -1;
            for (int s = 0; s < kSources; ++s) { const int rk = rank[b][s].load(); if (alive[s] && rk >= 0 && rk < kSources) slotAt[rk] = s; }
            for (int r = 0; r < kSources; ++r) if (slotAt[r] >= 0) order[n++] = slotAt[r];
            for (int role = 0; role < kNumRoles; ++role)
                for (int s = 0; s < kSources; ++s)
                    if (alive[s] && rank[b][s].load() < 0 && d.src[s].role.load() == role && n < kSources) order[n++] = s;
            float peak, sigma;
            curve (c.v (PRI0 + b), c.v (Q0 + b), n, peak, sigma);
            const float depth = c.v (DEPTH0 + b);
            float w[kSources], e[kSources];
            for (int i = 0; i < n; ++i) { w[i] = weight (i, peak, sigma); e[i] = d.src[order[i]].energy[b].load(); }
            for (int i = 0; i < n; ++i)
            {
                float cut = en ? maxCut * depth * (1.0f - w[i]) : 0.0f;
                if (cut > 0.0f && dynamic)
                {
                    float winner = -120.0f;
                    for (int j = 0; j < n; ++j) if (w[j] > w[i] + 0.05f) winner = std::max (winner, e[j]);
                    cut *= clamp01 ((winner - floorDb) / 18.0f);
                }
                cutOut[order[i]][b] = cut;
            }
        }
        for (int s = 0; s < kSources; ++s)
            for (int b = 0; b < kBands; ++b)
            {
                d.src[s].cut[b] = cutOut[s][b];
                dispE[s][b] = d.src[s].energy[b].load();
                dispCut[s][b] = cutOut[s][b];
            }
        for (int k = 0; k < kConflictSlots; ++k) d.conflicts[k].active = 0;
        dispFloor = floorDb;
    }

    // ---- state: global order + per-band orders. Entries are source keys ("#" + the plugin
    // instance's id, saved with its set), so a source keeps its place across sessions, track
    // renames and other sources of the same name. A plain name (sets saved before Nov 3) names
    // the source with that label and is replaced by its key when the source appears.
    void save (juce::ValueTree& t) const override
    {
        const std::lock_guard<std::mutex> lk (orderLock);
        auto join = [] (const std::vector<std::string>& v) { juce::String s; for (auto& nm : v) s << nm << "\n"; return s; };
        t.setProperty ("order", join (globalOrder), nullptr);
        for (int b = 0; b < kBands; ++b)
        {
            t.setProperty ("linked" + juce::String (b), linked[b], nullptr);
            t.setProperty ("order" + juce::String (b), join (bandOrder[b]), nullptr);
        }
    }
    void load (const juce::ValueTree& t) override
    {
        const std::lock_guard<std::mutex> lk (orderLock);
        auto split = [] (const juce::var& v) { std::vector<std::string> out; for (auto& l : juce::StringArray::fromLines (v.toString())) if (l.isNotEmpty()) out.push_back (l.toStdString()); return out; };
        globalOrder = split (t.getProperty ("order"));
        for (int b = 0; b < kBands; ++b)
        {
            linked[b] = (bool) t.getProperty ("linked" + juce::String (b), true);
            bandOrder[b] = split (t.getProperty ("order" + juce::String (b)));
        }
        updateRanks();
    }

    // ---- display (the editor's strip, top to bottom): the order (drag to change it), ALL BANDS /
    // LINK, the ten bands side by side (each lists its sources in its own order: their level, amber,
    // what is left after the cut, grey, and the priority curve), then every band's PRI, DEPTH, Q.
    int displayHeight() const override { return 470; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        const std::lock_guard<std::mutex> lk (orderLock);
        area = r;
        // List: global order, or the selected band's order.
        const auto rows = visibleRows (selBand);
        const float rh = rowH ((int) rows.size());
        auto list = listArea();
        const juce::String title = selBand < 0 ? "ORDER: ALL BANDS" : "ORDER: " + juce::String (bandName (selBand)) + " HZ" + (linked[selBand] ? " (ALL)" : " (OWN)");
        ui::frame (g, r, title);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto row = juce::Rectangle<float> (list.getX(), list.getY() + rh * (float) i, list.getWidth(), rh);
            const bool dragging = dragRow == (int) i;
            g.setColour (dragging ? ui::kText : ui::kLine);
            g.drawRect (row.reduced (0.0f, 1.0f), 1.0f);
            ui::label (g, juce::String (i + 1), row.removeFromLeft (18).withTrimmedLeft (4), ui::kFaint, 9.0f);
            ui::label (g, juce::String (rows[i].label), row, dragging ? ui::kAccent : ui::kText, 10.0f);
        }
        if (rows.empty()) ui::label (g, "NO SOURCES: SF TAP, SF STRIP OR AN SF INSTRUMENT", list.withHeight (20), ui::kFaint, 9.0f);
        // Buttons under the list.
        auto allB = allButton(), linkB = linkButton();
        g.setColour (selBand < 0 ? ui::kText : ui::kLine); g.drawRect (allB, 1.0f);
        ui::label (g, "ALL BANDS", allB, selBand < 0 ? ui::kText : ui::kDim, 9.5f, juce::Justification::centred);
        if (selBand >= 0)
        {
            g.setColour (ui::kLine); g.drawRect (linkB, 1.0f);
            ui::label (g, linked[selBand] ? "LINKED" : "LINK TO ALL", linkB, linked[selBand] ? ui::kFaint : ui::kText, 9.5f, juce::Justification::centred);
        }
        else ui::label (g, "CLICK A BAND: ITS OWN ORDER", linkB, ui::kFaint, 8.5f, juce::Justification::centred);

        for (int b = 0; b < kBands; ++b)
        {
            auto col = bandArea (b);
            const auto bandRows = visibleRows (b);
            const int n = (int) bandRows.size();
            // Header: click to select. White frame = selected; a dot = its own order.
            g.setColour (b == selBand ? ui::kText : ui::kLine);
            g.drawRect (col, b == selBand ? 1.5f : 1.0f);
            auto head = col.withHeight (14.0f);
            ui::label (g, bandName (b), head, b == selBand ? ui::kText : ui::kFaint, 8.5f, juce::Justification::centred);
            if (! linked[b]) { g.setColour (ui::kText); g.fillEllipse (col.getRight() - 5.0f, col.getY() + 2.0f, 3.0f, 3.0f); }
            const float lh = laneH (n);
            auto lanes = col.reduced (2.0f, 0.0f).withTrimmedTop (16.0f).withHeight (lh * (float) std::max (1, n));
            float peak, sigma;
            curve (getParam ? getParam (PRI0 + b) : 0.0f, getParam ? getParam (Q0 + b) : 0.5f, n, peak, sigma);
            const float depth = getParam ? getParam (DEPTH0 + b) : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const int s = bandRows[(size_t) i].slot;
                auto lane = juce::Rectangle<float> (lanes.getX(), lanes.getY() + lh * (float) i, lanes.getWidth(), lh).reduced (0.0f, 1.5f);
                g.setColour (ui::kGrid); g.fillRect (lane);
                const float pre = clamp01 ((dispE[s][b].load() - dispFloor.load()) / 50.0f);
                const float post = clamp01 ((dispE[s][b].load() - dispCut[s][b].load() - dispFloor.load()) / 50.0f);
                g.setColour (ui::kAmber); g.fillRect (lane.withWidth (lane.getWidth() * pre));
                g.setColour (ui::kTrack.brighter (0.9f)); g.fillRect (lane.withWidth (lane.getWidth() * post));
                ui::label (g, shortName (bandRows[(size_t) i].label).substring (0, 5), lane.withTrimmedLeft (1.0f), post > 0.35f ? ui::kBg : ui::kDim, 7.0f);
            }
            if (n > 0)
            {
                juce::Path p;
                const int steps = std::max (2, n * 8);
                for (int k = 0; k <= steps; ++k)
                {
                    const float rowPos = (float) k / (float) steps * (float) n - 0.5f;
                    const float dd = (rowPos - peak) / sigma;
                    const float wgt = 1.0f - depth * (1.0f - std::exp (-0.5f * dd * dd));
                    const float x = lanes.getX() + lanes.getWidth() * (0.15f + 0.85f * wgt);
                    const float y = lanes.getY() + lh * (rowPos + 0.5f);
                    if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                const bool touched = dragParam == PRI0 + b || dragParam == DEPTH0 + b || dragParam == Q0 + b;
                g.setColour (touched ? ui::kAccent : depth > 0.001f ? ui::kText : ui::kFaint);
                g.strokePath (p, juce::PathStrokeType (touched ? 2.0f : 1.2f));
            }
            for (int k = 0; k < 3; ++k)
            {
                const int idx = (k == 0 ? PRI0 : k == 1 ? DEPTH0 : Q0) + b;
                drawKnob (g, knobRect (b, k), getParam ? getParam (idx) : 0.0f, dragParam == idx);
            }
        }
        for (int k = 0; k < 3; ++k)
            ui::label (g, k == 0 ? "PRIORITY (TOP OF THE ORDER .. BOTTOM)" : k == 1 ? "DEPTH (HOW HARD THE OTHERS ARE CUT)" : "Q (WIDE: NEIGHBOURS SHARE)",
                       knobRect (0, k).withWidth (r.getWidth() - 16.0f).withHeight (10.0f).translated (0.0f, -11.0f), ui::kFaint, 8.0f);
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        const std::lock_guard<std::mutex> lk (orderLock);
        area = r;
        if (down)
        {
            dragParam = -1; dragRow = -1;
            for (int b = 0; b < kBands; ++b)
            {
                for (int k = 0; k < 3; ++k)
                    if (knobRect (b, k).contains (p)) { dragParam = (k == 0 ? PRI0 : k == 1 ? DEPTH0 : Q0) + b; startVal = getParam ? getParam (dragParam) : 0.0f; startY = p.y; return; }
                if (bandArea (b).contains (p)) { selBand = selBand == b ? -1 : b; return; }
            }
            if (allButton().contains (p)) { selBand = -1; return; }
            if (selBand >= 0 && linkButton().contains (p)) { linked[selBand] = true; bandOrder[selBand].clear(); updateRanks(); return; }
            const auto rows = visibleRows (selBand);
            if (listArea().contains (p))
            {
                const int i = (int) ((p.y - listArea().getY()) / rowH ((int) rows.size()));
                if (i >= 0 && i < (int) rows.size()) dragRow = i;
            }
            return;
        }
        if (dragParam >= 0 && setParam) setParam (dragParam, clamp01 (startVal + (startY - p.y) / 160.0f));
        if (dragRow >= 0)
        {
            const auto rows = visibleRows (selBand);
            const int target = juce::jlimit (0, std::max (0, (int) rows.size() - 1), (int) ((p.y - listArea().getY()) / rowH ((int) rows.size())));
            if (target != dragRow)
            {
                auto& ord = orderFor (selBand, true);
                // Make sure every visible row is in the stored order, then move the dragged source.
                for (auto& row : rows) if (std::find (ord.begin(), ord.end(), row.key) == ord.end()) ord.push_back (row.key);
                const std::string moving = rows[(size_t) dragRow].key, anchor = rows[(size_t) target].key;
                ord.erase (std::remove (ord.begin(), ord.end(), moving), ord.end());
                auto it = std::find (ord.begin(), ord.end(), anchor);
                if (target > dragRow && it != ord.end()) ++it;
                ord.insert (it, moving);
                dragRow = target;
                updateRanks();
            }
        }
    }
    void displayMouseUp() override { dragParam = -1; dragRow = -1; }

private:
    struct Row { std::string label, key; int slot; };
    static std::string keyFor (uint64_t uid) { char b[24]; std::snprintf (b, sizeof (b), "#%016llx", (unsigned long long) uid); return b; }
    // Does an order entry name this row? A key names one plugin instance; a plain name (older
    // sets) names the source carrying that label.
    static bool names (const std::string& e, const Row& r) { return e == r.key || (! e.empty() && e[0] != '#' && e == r.label); }

    static const char* bandName (int b) { static const char* n[kBands] = { "40", "80", "120", "180", "270", "390", "600", "1K", "2.5K", "9K" }; return n[b]; }
    static juce::String shortName (const std::string& s) { juce::String n (s); if (n.startsWith ("TAP ")) n = n.substring (4); if (n.startsWith ("STRIP ")) n = n.substring (6); return n.substring (0, 9); }

    // Editing a band's order unlinks it, starting from the global order.
    std::vector<std::string>& orderFor (int band, bool forEdit)
    {
        if (band < 0) return globalOrder;
        if (forEdit && linked[band]) { linked[band] = false; bandOrder[band] = globalOrder; }
        return linked[band] ? globalOrder : bandOrder[band];
    }
    const std::vector<std::string>& orderView (int band) const { return band < 0 || linked[band] ? globalOrder : bandOrder[band]; }

    // Geometry (the editor's strip, about 280 wide): the list keeps room for 8 sources (more: the
    // rows get shorter), the bands keep room for 8 lanes.
    juce::Rectangle<float> listArea() const { return { area.getX() + 6.0f, area.getY() + 18.0f, area.getWidth() - 12.0f, 8.0f * 17.0f }; }
    juce::Rectangle<float> allButton() const { auto l = listArea(); return { l.getX(), l.getBottom() + 6.0f, l.getWidth() * 0.4f, 18.0f }; }
    juce::Rectangle<float> linkButton() const { auto l = listArea(); return { l.getX() + l.getWidth() * 0.44f, l.getBottom() + 6.0f, l.getWidth() * 0.56f, 18.0f }; }
    float rowH (int n) const { return std::min (17.0f, listArea().getHeight() / (float) std::max (1, n)); }
    static float laneH (int n) { return std::min (14.0f, 8.0f * 14.0f / (float) std::max (1, n)); }
    juce::Rectangle<float> bandArea (int b) const
    {
        const float x0 = area.getX() + 6.0f, w = (area.getWidth() - 12.0f) / (float) kBands;
        return { x0 + w * (float) b, allButton().getBottom() + 8.0f, w - 2.0f, 16.0f + 8.0f * 14.0f + 2.0f };
    }
    juce::Rectangle<float> knobRect (int b, int k) const
    {
        auto col = bandArea (b);
        return { col.getX(), col.getBottom() + 18.0f + (float) k * 44.0f, col.getWidth(), 32.0f };
    }
    // A small knob: its arc, its value (0..100) under it.
    static void drawKnob (juce::Graphics& g, juce::Rectangle<float> r, float v, bool hot)
    {
        auto knob = r.removeFromTop (20.0f).withSizeKeepingCentre (18.0f, 18.0f);
        const float a0 = juce::degreesToRadians (225.0f), a1 = juce::degreesToRadians (495.0f);
        juce::Path track; track.addCentredArc (knob.getCentreX(), knob.getCentreY(), 8.0f, 8.0f, 0.0f, a0, a1, true);
        g.setColour (ui::kTrack); g.strokePath (track, juce::PathStrokeType (2.0f));
        juce::Path val; val.addCentredArc (knob.getCentreX(), knob.getCentreY(), 8.0f, 8.0f, 0.0f, a0, a0 + v * (a1 - a0), true);
        g.setColour (hot ? ui::kAccent : ui::kText); g.strokePath (val, juce::PathStrokeType (2.0f));
        ui::label (g, juce::String (juce::roundToInt (v * 100.0f)), r, hot ? ui::kAccent : ui::kDim, 8.0f, juce::Justification::centredTop);
    }

    // Alive sources with unique labels (duplicates get " 2", " 3"), by role.
    std::vector<Row> aliveRows() const
    {
        std::vector<Row> out;
        auto& d = Bus::get().d();
        std::vector<std::pair<int, int>> byRole;
        for (int s = 0; s < kSources; ++s) if (Bus::get().sourceAlive (s)) byRole.push_back ({ d.src[s].role.load(), s });
        std::sort (byRole.begin(), byRole.end());
        std::map<std::string, int> seen;
        for (auto& rs : byRole)
        {
            char nm[24]; Bus::unpackName (d.src[rs.second].name, nm);
            std::string label = nm;
            const int k = ++seen[label];
            if (k > 1) label += " " + std::to_string (k);
            const uint64_t uid = d.src[rs.second].uid.load();
            out.push_back ({ label, uid != 0 ? keyFor (uid) : label, rs.second });
        }
        return out;
    }
    // Rows in a band's order (-1 = global); sources the order doesn't name yet come last.
    std::vector<Row> visibleRows (int band) const
    {
        const auto alive = aliveRows();
        const auto& ord = orderView (band);
        std::vector<Row> rows;
        std::vector<char> placed (alive.size(), 0);
        for (auto& e : ord)
            for (size_t i = 0; i < alive.size(); ++i)
                if (! placed[i] && names (e, alive[i])) { rows.push_back (alive[i]); placed[i] = 1; break; }
        for (size_t i = 0; i < alive.size(); ++i) if (! placed[i]) rows.push_back (alive[i]);
        return rows;
    }
    void updateRanks()
    {
        for (int b = 0; b < kBands; ++b)
        {
            const auto rows = visibleRows (b);
            int r[kSources]; for (auto& x : r) x = -1;
            for (size_t i = 0; i < rows.size(); ++i) r[rows[i].slot] = (int) i;
            for (int s = 0; s < kSources; ++s) rank[b][s] = r[s];
        }
    }
    void timerCallback() override
    {
        const std::lock_guard<std::mutex> lk (orderLock);
        // Newcomers join the end of every order, so lists only change when you drag them. A plain
        // name from an older set becomes the key of the source carrying it, in place.
        const auto alive = aliveRows();
        auto adopt = [&] (std::vector<std::string>& ord)
        {
            for (auto& row : alive)
            {
                if (std::find (ord.begin(), ord.end(), row.key) != ord.end()) continue;
                auto it = std::find_if (ord.begin(), ord.end(), [&] (const std::string& e) { return ! e.empty() && e[0] != '#' && e == row.label; });
                if (it != ord.end()) *it = row.key; else ord.push_back (row.key);
            }
        };
        adopt (globalOrder);
        for (int b = 0; b < kBands; ++b) if (! linked[b]) adopt (bandOrder[b]);
        updateRanks();
    }

    double sr = 48000.0;
    // The orders are touched by the message thread (timer, editor) and the host's state calls,
    // never by the audio thread (it reads rank[]).
    mutable std::mutex orderLock;
    std::vector<std::string> globalOrder, bandOrder[kBands];
    bool linked[kBands];
    std::atomic<int> rank[kBands][kSources];
    float cutOut[kSources][kBands] {};
    juce::Rectangle<float> area;
    int dragParam = -1, dragRow = -1, selBand = -1;
    float startVal = 0.0f, startY = 0.0f;
    std::atomic<float> dispE[kSources][kBands] {}, dispCut[kSources][kBands] {}, dispFloor { -50.0f };
};
ModulePtr makeALLOC() { return std::make_unique<AllocModule>(); }

// =====================================================================================  TAP
// Puts any Live track (stems, hardware, other plugins) into Pocket and ALLOC. Makes no sound.
class TapModule : public Module
{
public:
    enum { ROLE, ALLOCON, PDEPTH, PSHAPE, LEVEL };
    std::string id() const override { return "TAP"; }
    std::vector<ParamSpec> params() const override
    {
        std::vector<std::string> roles; for (int i = 0; i < kNumRoles; ++i) roles.push_back (kRoleNames[i]);
        auto r = choice ("role", "ROLE", roles, ROLE_OTHER); r.primary = true; r.song = false; r.preset = false;
        return { r, toggle ("alloc", "ALLOC", true).noPreset().prim(), pct ("pocket_depth", "POCKET DEPTH", 0.0f).prim(),
                 pct ("pocket_shape", "POCKET SHAPE", 0.3f).prim(), levelParam().prim() };
    }
    int latency() const override { return Pocket::kLatency; }
    bool takesDropCut (const Ctx& c) const override { return c.ci (ROLE) != ROLE_DECK; }   // a source, as an instrument
    void prepare (double rate, int mb) override { pocket.prepare (rate, mb); tap.prepare (rate); }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int role = c.ci (ROLE);
        if (role != lastRole) { lastRole = role; tap.setIdentity (role, (std::string ("TAP ") + kRoleNames[role]).c_str()); }
        pocket.dropDuck = dropDucks (role);
        pocket.process (c, io.getArrayOfWritePointers(), 2, c.n, c.v (PDEPTH), c.v (PSHAPE), nullptr, c.v (PDEPTH) > 0.001f);
        tap.process (io.getArrayOfWritePointers(), 2, c.n, c.b (ALLOCON), &c);
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
    }
    int displayHeight() const override { return 80; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override { pocket.paint (g, r); }
private:
    Pocket pocket;
    AllocTap tap;
    int lastRole = -1;
};
ModulePtr makeTAP() { return std::make_unique<TapModule>(); }
} // namespace sf
