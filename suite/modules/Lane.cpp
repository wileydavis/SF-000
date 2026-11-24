// (SF-000, the hundred and third build) Every lane's instrument. Wiley, Nov 21: "build it as if the only engines are the
// three (forge, sampler, and synth) ... strip out anything that isn't needed for these ... every lane gets the three models
// and nothing else". One module for all eleven lanes: MODEL and LAYER MODEL each one of SAMPLE, SYNTH and KICK FORGE
// (SampleEngine, SynthEngine, ForgeEngine, the same on every lane), LAYER and LAYER SPLIT between them; and what a lane is
// besides its sound, which every lane has alike: DENSITY's echoes, VEL SENS, KEY FOLLOW, MONO, the lane's FILTER, RES and
// LEVEL, and on the lanes that duck under the kick its POCKET. What differs by lane is only its part in the rig: KICK tells
// the bus of its hits (the pockets and RUMBLE follow them); RUMBLE can be struck by them (SOURCE); the synth lanes, RUMBLE
// and VOICE duck under the kick; the drum lanes' ninth knob is DECAY, a pitched lane's GATE.
// Replaces the suite's KICK, PERC, STRIKE, RUMBLE, VOICE, BASS, LEAD and PAD in SF-000 (their models went with them).
#include "Kit.h"
#include "SampleEngine.h"
#include "SynthEngine.h"
#include "ForgeEngine.h"
#include "../core/Instrument.h"
#include "../core/Registry.h"

namespace sf
{
namespace
{
enum EngineKind { kSample = 0, kSynth = 1, kForge = 2 };
const std::vector<std::string>& engineNames() { static const std::vector<std::string> n { "SAMPLE", "SYNTH", "KICK FORGE" }; return n; }

struct LaneRole
{
    const char* id;         // the module's name (the rig's lanes name these)
    int role;               // the bus's
    bool pitched;           // PLAYS as it opens: NOTES (GATE, and each engine plays the note's pitch), or HITS
    bool pocket;            // ducks under the kick (POCKET DEPTH, SHAPE, AFTER FX)
    bool publishes;         // tells the bus of its hits (KICK)
    bool follows;           // can be struck by the kick's hits (RUMBLE: SOURCE)
    int model;              // MODEL as it opens
    float level;            // LEVEL as it opens (dB)
};
}

class LaneModule : public Module
{
public:
    explicit LaneModule (const LaneRole& r) : role (r) {}
    std::string id() const override { return role.id; }
    bool instrument() const override { return true; }
    int latency() const override { return role.pocket || role.follows ? Pocket::kLatency : 0; }
    double tailSeconds() const override { return 6.0; }

    std::vector<ParamSpec> params() const override
    {
        const auto& mn = engineNames();
        std::vector<ParamSpec> p {
            choice ("model", "MODEL", mn, role.model),
            // (LAYER MODEL changed under a key that is held plays the key again on the new engine: a hit is struck once, and
            // a drum lane's new layer is heard from its next hit)
            role.pitched ? choice ("layer_model", "LAYER MODEL", mn, role.model == kSynth ? kSample : kSynth).eased().layerGroup()
                         : choice ("layer_model", "LAYER MODEL", mn, role.model == kSynth ? kSample : kSynth).next().layerGroup(),
            pct ("layer", "LAYER", 0.0f).slow().layerGroup(),
            knob ("split", "SPLIT", 0.0f, 1.0f, 0.5f, "SPLIT").layerGroup(),
            knob ("split_hz", "SPLIT HZ", 50.0f, 800.0f, 150.0f, "Hz", 200.0f).layerGroup(),
            role.pitched ? pct ("gate", "GATE", 0.5f).prim().nextOn ("SAMPLE").nextOn ("SYNTH").nextOn ("KICK FORGE")
                         : pct ("decay", "DECAY", 0.5f).next().prim().dir (1),
            pct ("density", "DENSITY", 0.0f).next().prim().dir (1),
            bip ("filter", "FILTER").prim(),
            pct ("res", "FILTER RES", 0.1f),
            levelParam (role.level).prim(),
            pct ("echo_prob", "ECHO PROB", 1.0f).next().slow(),
            choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 1).next(),
            pct ("echo_swing", "ECHO SWING", 0.0f).next().slow(),
            pct ("vel_sens", "VEL SENS", role.pitched ? 1.0f : 0.5f).next(),
            toggle ("key_follow", "KEY FOLLOW", true).next().noPreset().forget(),
            toggle ("mono", "MONO", role.role == ROLE_BASS).eased().nextOn ("SAMPLE").nextOn ("SYNTH").noPreset().forget(),      // (SYNTH: a chord that sounds keeps its voices)
            // (SF-000, the hundred and twelfth build) Wiley, Nov 23: "every lane gets exactyl the same engines, so that the same
            // settings in any lane will produce the same sound". What a drum lane did and a pitched one did is a setting now,
            // on every lane: HITS (each struck for the ninth knob's DECAY, the step's own note set aside; SAMPLE at its file's
            // pitch, KEY FOLLOW moving it to the key's nearest note) or NOTES (held for GATE, at the note's pitch, KEY FOLLOW
            // moving it with the key). Each lane opens as it was. The ninth knob is the same control either way, read as
            // DECAY on HITS and GATE on NOTES.
            choice ("plays", "PLAYS", { "HITS", "NOTES" }, role.pitched ? 1 : 0).next().noPreset() };
        if (role.pocket)
        {
            p.push_back (pct ("pocket_depth", "POCKET DEPTH", role.role == ROLE_BASS ? 0.35f : role.role == ROLE_RUMBLE ? 0.6f : 0.2f).slow());
            p.push_back (pct ("pocket_shape", "POCKET SHAPE", 0.25f));
            p.push_back (toggle ("pocket_post", "AFTER FX", false).noPreset());      // (the pocket left to the host, after the lane's effects)
        }
        if (role.follows) p.push_back (choice ("source", "SOURCE", { "KICK BUS", "MIDI", "BOTH" }, 0).noPreset().forget());
        return frg::with (syn::with (smp::with (std::move (p))));
    }

    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP A SAMPLE: SAMPLE PLAYS IT; SYNTH'S TABLES AND THE FORGE'S IMPACT READ IT"; }
    void loadFiles (const juce::StringArray& f) override { if (f.size() > 0) { sample.set (loadSampleFile (juce::File (f[0]), sr, 300.0)); sy.fileChanged (sample.get()); } }
    void save (juce::ValueTree& t) const override { sample.save (t, "SAMPLE"); }
    void load (const juce::ValueTree& t) override { sample.load (t, "SAMPLE", sr); sy.fileChanged (sample.get()); }
    bool holdsNotes() const override { return holds.load(); }

    void prepare (double rate, int mb) override
    {
        if (auto s = sample.get(); s != nullptr && std::abs (s->sr - rate) > 1 && s->flac.getSize() > 0) sample.set (loadSampleFlac (s->flac, s->name, rate, s->flacGain));
        sr = rate;
        const auto specs = params();
        auto at = [&specs] (const char* id) { for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == id) return (int) i; return -1; };
        MODEL = at ("model"); LAYERMODEL = at ("layer_model"); LAYER = at ("layer"); SPLIT = at ("split"); SPLITHZ = at ("split_hz");
        HOLD = role.pitched ? at ("gate") : at ("decay"); DENS = at ("density"); FILT = at ("filter"); RES = at ("res"); LEVEL = at ("level");
        EPROB = at ("echo_prob"); ESPACE = at ("echo_space"); ESWING = at ("echo_swing"); VELSENS = at ("vel_sens"); KEYFOLLOW = at ("key_follow");
        MONO = at ("mono"); PLAYS = at ("plays"); PDEPTH = at ("pocket_depth"); PSHAPE = at ("pocket_shape"); PPOST = at ("pocket_post"); SOURCE = at ("source");
        // (RUMBLE's SYNTH and KICK FORGE are pitched by what strikes it, the kick's own note; its SAMPLE plays the file at
        // the file's pitch, TUNE moving it, as a drum lane's does: a recorded rumble taken two octaves down to G1 is a smear)
        const bool engPitched = role.pitched || role.follows;
        notesNow = role.pitched;
        eng.bind (specs, role.pitched, role.pitched ? SampleEngine::Ends::keys : SampleEngine::Ends::hits);
        eng.prepare (rate, mb);
        sy.bind (specs, engPitched, role.pitched ? SynthEngine::Ends::keys : SynthEngine::Ends::hits);
        sy.prepare (rate, mb);
        sy.fileChanged (sample.get());
        fg.bind (specs, engPitched);
        fg.prepare (rate, mb);
        out.prepare (rate, mb, role.role, role.id, role.pocket || role.follows);
        split.prepare (rate);
        events.reserve (2048);
        delayed.reserve (256);
        for (auto& b : partBuf) b.setSize (2, std::max (64, mb), false, true, true);
        reset();
    }
    void reset() override
    {
        echoes.reset();
        eng.reset(); sy.reset(); fg.reset();
        playsWas = -1;
        out.reset();
        numDown = 0; lastA = lastB = -1; layerWas = false; autoOffWas = -2;
        delayed.clear(); lastTrigPpq = -1.0e9;
    }

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midiIn) override
    {
        const int mA = juce::jlimit (0, 2, c.ci (MODEL)), mB = juce::jlimit (0, 2, c.ci (LAYERMODEL));
        const float layer = c.v (LAYER);
        // (a layer of the same engine as the model is the same sound again: nothing is added, and nothing is listed for it)
        const bool layerOn = layer > 0.01f && mB != mA;
        const SampleData* smp = sample.audio();
        // RUMBLE's SOURCE: the kick's hits and its own notes on one timeline, a pocket's latency late (the kick is heard
        // where it was told; RoleOutput then does not delay the audio again)
        juce::MidiBuffer followed;
        juce::MidiBuffer& midi = role.follows ? followed : midiIn;
        if (role.follows) gatherFollowed (c, midiIn, followed);
        // PLAYS (the hundred and twelfth build): turned, what sounds lets go and the engines take it from the next note
        const bool notes = PLAYS >= 0 ? c.ci (PLAYS) == 1 : role.pitched;
        if (playsWas >= 0 && (playsWas == 1) != notes)
        {
            allOff (kSample, 0); allOff (kSynth, 0);
            numDown = 0; autoOffWas = -2;
        }
        if (playsWas < 0 || (playsWas == 1) != notes)
        {
            notesNow = notes;
            const bool engPitched = notes || role.follows;
            eng.plays (notes, notes ? SampleEngine::Ends::keys : SampleEngine::Ends::hits);
            sy.plays (engPitched, notes ? SynthEngine::Ends::keys : SynthEngine::Ends::hits);
            fg.plays (engPitched);
        }
        playsWas = notes ? 1 : 0;
        echoes.run (c, midi, events, c.v (DENS), echoSpaceBeats (c.ci (ESPACE)), c.v (ESWING), c.v (EPROB), notes);
        // the engines, for this block: the lane's ninth knob, MONO, and the key
        const bool keyOn = c.b (KEYFOLLOW) && c.keyRoot >= 0;
        const int keyShift = notes && keyOn ? keyOffset (c.keyRoot) : 0;
        const float hold = c.v (HOLD);
        const bool mono = c.b (MONO);
        eng.begin (c, smp, hold, mono, keyShift);
        sy.begin (c, hold, mono, keyShift);
        fg.begin (c, hold, smp, keyShift, keyOn && ! notes ? c.keyRoot : -1);
        int autoOff = -1; float relMul = 1.0f;
        if (notes) gateShape (hold, c.samplesPerBeat(), autoOff, relMul);
        eng.releaseScale (relMul); sy.releaseScale (relMul);
        // (SF-000, the hundred and fourth build) KICK FORGE is struck, never held: a lane holds its notes only where an engine
        // that sounds holds them (a step longer than its sequence ties on into a drone only then; KICK FORGE strikes again)
        const auto holdsOn = [&] (int e) { return e == kSample ? eng.holds() : e == kSynth && sy.holds(); };      // (SYNTH: one that sustains, the hundred and fifteenth build)
        holds.store (holdsOn (mA) || (layerOn && holdsOn (mB)));
        // MODEL or LAYER MODEL changed (or LAYER brought up or down): what sounds on the old one lets go, and the keys still
        // down start again on the new, each at its own velocity (a held chord goes on in the new sound)
        if (lastA < 0) { lastA = mA; lastB = mB; layerWas = layerOn; }
        if (mA != lastA || mB != lastB || layerOn != layerWas)
        {
            const bool aChanged = mA != lastA;
            const bool bChanged = mB != lastB || layerOn != layerWas;
            if (aChanged) allOff (lastA, 0);
            if (bChanged && layerWas) allOff (lastB, 1);
            lastA = mA; lastB = mB; layerWas = layerOn;
            if (notes)
                for (int k = 0; k < numDown; ++k)
                {
                    if (aChanged) strike (mA, 0, 0, down[(size_t) k].note, down[(size_t) k].vel, 1.0f, 0, autoOff, keyOn, c);
                    if (bChanged && layerOn) strike (mB, 0, 1, down[(size_t) k].note, down[(size_t) k].vel, 1.0f, 0, autoOff, keyOn, c);
                }
        }
        // GATE turned as notes sound (the ninety-second and ninety-third builds, on every engine that holds a key): its lower
        // half ends a sounding note where the knob now says, counted from where it began; brought back over half way each
        // note is its key's again, and a key still down whose note had ended plays again (Wiley, Nov 19: "it went away but
        // the sound didn't comr backl when i turned the gate up")
        if (notes && autoOff != autoOffWas)
        {
            int keys[16]; const int nk = std::min (numDown, 16);
            for (int k = 0; k < nk; ++k) keys[k] = down[(size_t) k].note;
            eng.gateNow (autoOff, keys, nk); sy.gateNow (autoOff, keys, nk);
            if (autoOffWas >= 0 && autoOff < 0)
                for (int k = 0; k < numDown; ++k)
                {
                    if (mono && k != numDown - 1) continue;           // (one voice: the newest key stands for them all)
                    const int key = down[(size_t) k].note;
                    auto plays = [&] (int e, int part) { return e == kSample ? eng.keySounding (key, part) : e == kSynth ? sy.keySounding (key, part) : true; };
                    if (! plays (mA, 0)) strike (mA, 0, 0, key, down[(size_t) k].vel, 1.0f, 0, autoOff, keyOn, c);
                    if (layerOn && mB != mA && ! plays (mB, 1)) strike (mB, 0, 1, key, down[(size_t) k].vel, 1.0f, 0, autoOff, keyOn, c);
                }
        }
        autoOffWas = autoOff;
        const float velSens = c.v (VELSENS);
        for (const auto& ev : events)
        {
            if (ev.gen == 0 && notes)
            {
                if (ev.on) keyDown (ev.note, ev.vel);
                else if (ev.note < 0) numDown = 0;
                else keyUp (ev.note);
            }
            if (ev.on)
            {
                const float vel = ev.gen == 0 ? lerp (1.0f, ev.vel, velSens) : ev.vel;
                strike (mA, ev.offset, 0, ev.note, vel, ev.gain, ev.gen, autoOff, keyOn, c);
                if (layerOn) strike (mB, ev.offset, 1, ev.note, vel, ev.gain, ev.gen, autoOff, keyOn, c);
                if (role.publishes) publish (c, mA, ev.offset, ev.note, vel * ev.gain, ev.gen, keyOn, smp);
            }
            else
            {
                eng.noteOff (ev.offset, ev.note, ev.gen);
                sy.noteOff (ev.offset, ev.note, ev.gen);
            }
        }
        // each engine into its part (MODEL's, the layer's), then LAYER SPLIT between them
        const int n = c.n;
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        float* aL = partBuf[0].getWritePointer (0); float* aR = partBuf[0].getWritePointer (1);
        float* bL = partBuf[1].getWritePointer (0); float* bR = partBuf[1].getWritePointer (1);
        std::fill (aL, aL + n, 0.0f); std::fill (aR, aR + n, 0.0f); std::fill (bL, bL + n, 0.0f); std::fill (bR, bR + n, 0.0f);
        auto renderInto = [&] (int e, float* l, float* r, float g)
        {
            if (e == kSample && (eng.sounding() || mA == kSample || (layerOn && mB == kSample))) eng.render (c, l, r, n, g);
            if (e == kSynth && (sy.sounding() || mA == kSynth || (layerOn && mB == kSynth))) sy.render (c, l, r, n, g);
            if (e == kForge && (fg.sounding() || mA == kForge || (layerOn && mB == kForge))) fg.render (l, r, n, g);
        };
        // (an engine that is neither, ringing out after it was let go, goes with MODEL's)
        for (int e = 0; e < 3; ++e)
            if (! (layerOn && e == mB)) renderInto (e, aL, aR, layer);
        if (layerOn) renderInto (mB, bL, bR, layer);
        const float k0 = c.v0 (SPLIT), k1 = c.v (SPLIT);
        split.set (c.v (SPLITHZ), 1.0f / (1.0f + layer), LayerSplit::amount (k1));
        const bool splitOff = ! layerOn || split.off (LayerSplit::amount (k0), LayerSplit::amount (k1));
        for (int i = 0; i < n; ++i)
        {
            float l = aL[i], r = aR[i];
            if (splitOff) { l += bL[i]; r += bR[i]; }
            else split.tick (l, r, bL[i], bR[i], LayerSplit::amount (k0 + (k1 - k0) * (float) i / (float) n));
            L[i] = l; R[i] = r;
            hist.push (std::fabs (0.5f * (l + r)));
        }
        const float pocket = role.pocket && ! c.b (PPOST) ? c.v (PDEPTH) : 0.0f;
        out.process (c, io, FILT, RES, pocket, role.pocket ? c.v (PSHAPE) : 0.0f, false, LEVEL, ! role.follows);
    }

    // ---- the display: SYNTH's or SAMPLE's while it is the model; KICK FORGE's, what the lane gives out
    int displayHeight() const override { const int m = modelNow(); return m == kSynth ? SynthEngine::kDisplayHeight : m == kSample ? SampleEngine::kDisplayHeight : kOwn; }
    bool displayWheel (juce::Point<float> p, juce::Rectangle<float> r, float dy) override
    {
        const int m = modelNow();
        if (m == kSynth) return sy.wheel (p, r, dy, *this);
        if (m == kSample) return eng.wheel (p, r, dy, sample.get().get());
        return false;
    }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        const int m = modelNow();
        if (m == kSynth) { sy.paint (g, r, *this, true); return; }
        if (m == kSample) { eng.paint (g, r, *this, sample.get().get(), true); return; }
        r = r.withHeight ((float) kOwn);
        ui::frame (g, r, "KICK FORGE");
        auto in = r.reduced (8, 18);
        juce::Path p;
        constexpr int N = 512;
        for (int i = 0; i < N; ++i)
        {
            const float v = hist.v[(hist.w + i) % N].load();
            const float x = in.getX() + in.getWidth() * (float) i / (float) (N - 1);
            if (i == 0) p.startNewSubPath (x, in.getBottom() - v * in.getHeight()); else p.lineTo (x, in.getBottom() - v * in.getHeight());
        }
        g.setColour (ui::kText); g.strokePath (p, juce::PathStrokeType (1.0f));
        const auto s = sample.get();
        ui::label (g, s == nullptr ? juce::String ("IMPACT: NOISE (NO FILE ON THIS LANE)") : "IMPACT FROM " + s->name.toUpperCase(),
                   r.reduced (8, 2).removeFromBottom (14), ui::kText, 10.0f, juce::Justification::centredRight);
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        const int m = modelNow();
        if (m == kSynth) sy.mouse (p, r, down, *this);
        else if (m == kSample) eng.mouse (p, r, down, *this, sample.get().get());
    }
    void displayMouseUp() override { eng.mouseUp(); sy.mouseUp(); }
    void displayDouble (juce::Point<float> p, juce::Rectangle<float> r) override { if (modelNow() == kSample) eng.doubleClick (p, r, *this, sample.get().get()); }

private:
    int modelNow() const { return getParam && MODEL >= 0 ? juce::jlimit (0, 2, (int) std::lround (getParam (MODEL))) : role.model; }
    void allOff (int e, int)
    {
        if (e == kSample) eng.noteOff (0, -1, 0);
        if (e == kSynth) sy.noteOff (0, -1, 0);
    }
    // one note on one engine: its pitch (a drum lane with KEY FOLLOW: moved to the key's nearest note, as the kick's was)
    void strike (int e, int offset, int part, int note, float vel, float gain, int gen, int autoOff, bool keyOn, const Ctx& c)
    {
        float pitch = (float) note;
        if (! notesNow && ! role.follows && keyOn && e != kForge) pitch += keySnap (440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f), c.keyRoot);
        if (e == kSample) eng.noteOn (offset, note, pitch, vel, gain, gen, part, autoOff);
        else if (e == kSynth) sy.noteOn (offset, note, pitch, vel, gain, gen, part, autoOff);
        else fg.noteOn (offset, pitch, vel, gain, gen, part);
    }
    // KICK: each hit told to the bus, as the kick's always were (the pockets duck under it, RUMBLE is struck by it): the
    // pitch it falls from and to, how long; the lane's FILTER's corners
    void publish (Ctx& c, int e, int offset, int note, float vel, int gen, bool keyOn, const SampleData* s)
    {
        if (c.bus == nullptr || c.inCut (c.ppqAt (offset))) return;
        float hp, lp;
        DJFilter::corners (c.v (FILT), hp, lp);
        float f0 = 100.0f, f1 = 50.0f, tau = 0.012f, dec = 0.3f;
        if (e == kForge) fg.shapeOf ((float) note, gen, f0, f1, tau, dec);
        else if (e == kSample && s != nullptr)
        {
            const float rate = semis (eng.ids.tune >= 0 ? c.v (eng.ids.tune) : 0.0f);
            f1 = s->rootHz > 20 && s->rootHz < 200 ? s->rootHz * rate : 50.0f; f0 = 2.0f * f1;
            dec = std::max (0.02f, std::min (s->decaySec * 0.4f, sampleGateSeconds (c.v (HOLD), (float) (s->length() / s->sr) / rate, 0)));
        }
        else if (e == kSynth)
        {
            float st = (float) note;
            if (keyOn) st += keySnap (440.0f * std::pow (2.0f, (st - 69.0f) / 12.0f), c.keyRoot);
            f1 = juce::jlimit (20.0f, 200.0f, 440.0f * std::pow (2.0f, (st - 69.0f) / 12.0f)); f0 = 2.0f * f1;
            dec = 0.02f * std::pow (100.0f, c.v (HOLD));
        }
        c.bus->publishKick ({ c.ppqAt (offset), std::min (1.0f, vel), f0, f1, tau, std::min (dec, lp < 200.0f ? 0.05f : dec), hp, lp });
    }
    // RUMBLE's SOURCE: the kick's hits, and the lane's own notes, at their times a pocket's latency late, as note messages
    // (the kick's at the note it falls to)
    void gatherFollowed (Ctx& c, juce::MidiBuffer& midi, juce::MidiBuffer& outMidi)
    {
        const int src = SOURCE >= 0 ? c.ci (SOURCE) : 0;
        const double bps = c.beatsPerSample();
        const double secPerBeat = 60.0 / c.bpm;
        const double look = Pocket::kLatency * bps + (double) c.bus->d().pocketOffsetMs.load() * 0.001 / secPerBeat;
        const double lookSec = Pocket::kLatency / sr;
        if (c.ppq - look < lastTrigPpq - 0.25) { lastTrigPpq = c.ppq - look - 1.0e-6; delayed.clear(); }
        if (src != 0)
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if ((m.isNoteOn() || m.isNoteOff()) && delayed.size() < 250) delayed.push_back ({ c.ppqAt (meta.samplePosition), m });
                else if (m.isAllNotesOff() || m.isAllSoundOff()) outMidi.addEvent (m, meta.samplePosition);
            }
        KickHit hits[kKickRing];
        const int nh = (src != 1 && c.bus->kickAlive (4000)) ? c.bus->recentKicks (hits, kKickRing) : 0;
        for (int i = 0; i < c.n; ++i)
        {
            const double tq = c.ppqAt (i) - look;
            for (int h = nh - 1; h >= 0; --h)
                if (hits[h].ppq > lastTrigPpq + 1.0e-6 && hits[h].ppq <= tq && tq - hits[h].ppq < 0.25 && Bus::kickCurrent (hits[h], tq - hits[h].ppq, secPerBeat, lookSec))
                {
                    lastTrigPpq = hits[h].ppq;
                    const int note = juce::jlimit (0, 127, (int) std::lround (69.0f + 12.0f * std::log2 (std::max (16.0f, hits[h].f1) / 440.0f)));
                    outMidi.addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.05f, 1.0f, hits[h].vel)), i);
                    outMidi.addEvent (juce::MidiMessage::noteOff (1, note), std::min (c.n - 1, i + 1));
                }
            for (size_t k = 0; k < delayed.size();)
                if (delayed[k].first <= tq) { outMidi.addEvent (delayed[k].second, i); delayed.erase (delayed.begin() + (long) k); }
                else ++k;
        }
    }
    void keyDown (int note, float vel)
    {
        keyUp (note);
        if (numDown < (int) down.size()) down[(size_t) numDown++] = { note, vel };
    }
    void keyUp (int note)
    {
        for (int k = 0; k < numDown; ++k)
            if (down[(size_t) k].note == note) { for (int j = k; j < numDown - 1; ++j) down[(size_t) j] = down[(size_t) j + 1]; --numDown; return; }
    }

    const LaneRole role;
    int MODEL = -1, LAYERMODEL = -1, LAYER = -1, SPLIT = -1, SPLITHZ = -1, HOLD = -1, DENS = -1, FILT = -1, RES = -1, LEVEL = -1;
    int EPROB = -1, ESPACE = -1, ESWING = -1, VELSENS = -1, KEYFOLLOW = -1, MONO = -1, PDEPTH = -1, PSHAPE = -1, PPOST = -1, SOURCE = -1, PLAYS = -1;
    int playsWas = -1;          // (PLAYS a block ago: 1 NOTES, 0 HITS, -1 not yet)
    bool notesNow = true;
    double sr = 48000.0;
    SampleSlot sample;
    SampleEngine eng;
    SynthEngine sy;
    ForgeEngine fg;
    EchoScheduler echoes;
    std::vector<NoteEvent> events;
    RoleOutput out;
    LayerSplit split;
    juce::AudioBuffer<float> partBuf[2];
    struct Down { int note; float vel; };
    std::array<Down, 64> down {};
    int numDown = 0, lastA = -1, lastB = -1, autoOffWas = -2;      // (autoOffWas: GATE's length a block ago, -2 none yet)
    bool layerWas = false;
    std::atomic<bool> holds { true };
    std::vector<std::pair<double, juce::MidiMessage>> delayed;
    double lastTrigPpq = -1.0e9;
    static constexpr int kOwn = 70;
    struct DecimHist { std::atomic<float> v[512] {}; int w = 0, k = 0; float acc = 0; void push (float x) { acc = std::max (acc, x); if (++k >= 512) { v[w].store (std::min (1.0f, acc)); w = (w + 1) % 512; k = 0; acc = 0; } } };
    DecimHist hist;
};

// The rig's lanes, by the names they had: what each is in the rig, and how it opens
namespace
{
ModulePtr lane (const LaneRole& r) { return std::make_unique<LaneModule> (r); }
}
ModulePtr makeKICK()   { return lane ({ "KICK",   ROLE_KICK,   false, false, true,  false, kForge, -3.0f }); }
ModulePtr makePERC()   { return lane ({ "PERC",   ROLE_PERC,   false, false, false, false, kSynth, -13.0f }); }
ModulePtr makeSTRIKE() { return lane ({ "STRIKE", ROLE_PERC,   false, false, false, false, kSynth, -14.0f }); }
ModulePtr makeRUMBLE() { return lane ({ "RUMBLE", ROLE_RUMBLE, false, true,  false, true,  kForge, -12.0f }); }
ModulePtr makeVOICE()  { return lane ({ "VOICE",  ROLE_VOICE,  true,  true,  false, false, kSample, -6.0f }); }
ModulePtr makeBASS()   { return lane ({ "BASS",   ROLE_BASS,   true,  true,  false, false, kSynth, -6.0f }); }
ModulePtr makeLEAD()   { return lane ({ "LEAD",   ROLE_LEAD,   true,  true,  false, false, kSynth, -6.0f }); }
ModulePtr makePAD()    { return lane ({ "PAD",    ROLE_PAD,    true,  true,  false, false, kSynth, -6.0f }); }
} // namespace sf
