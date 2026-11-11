// VOICE: spoken-word / vocal instrument. A loaded phrase is sliced (C3 = slice 1, C#3 = slice 2 ...,
// keys below C3 play the whole phrase). Models process it: SAMPLE, RADIO, VOCODER, ROBOT, CHOIR,
// STUTTER. Built-in dub echo send. With no phrase loaded it plays a formant choir from the notes.
// Carriers are band-limited (polyBLEP), phrases read with Hermite interpolation, DRIVE runs 2x
// oversampled. KEY FOLLOW moves the carriers and the choir to the SF CONTROL key (phrases keep
// their own pitch; TUNE moves them).
#include "Kit.h"
#include "../core/BandLimited.h"
#include "../core/Oversample.h"
#include "Calibration.h"

namespace sf
{
class VoiceModule : public Module
{
public:
    // SSTART: the phrase's START; END, FADE IN, FADE OUT follow (sampleRegionParams).
    enum { SIGNAL, TONE, GATE, DENS, FILT, ECHO, DRIVE, LEVEL, MODEL, SLICEMODE, TUNE, ECHOTIME, ECHOFB, PDEPTH, PSHAPE, ESPACE, ALLOCON, KEYFOLLOW, SSTART };
    std::string id() const override { return "VOICE"; }
    bool instrument() const override { return true; }
    int latency() const override { return Pocket::kLatency; }
    double tailSeconds() const override { return 10.0; }
    std::vector<ParamSpec> params() const override
    {
        return withRegion ({ pct ("signal", "SIGNAL", 0.6f).prim().dir (1),
                 pct ("tone", "TONE", 0.5f).prim(),
                 pct ("gate", "GATE", 0.6f).prim(),
                 pct ("density", "DENSITY", 0.0f).prim().dir (1),
                 bip ("filter", "FILTER").prim(),
                 pct ("echo", "ECHO", 0.2f).prim().dir (1),
                 pct ("drive", "DRIVE", 0.1f).prim(),
                 levelParam (-6.0f).prim(),
                 choice ("model", "MODEL", { "SAMPLE", "RADIO", "VOCODER", "ROBOT", "CHOIR", "STUTTER" }, 1),
                 choice ("slice_mode", "SLICE MODE", { "AUTO", "EQUAL 8", "EQUAL 16", "WHOLE" }, 0),
                 knob ("tune", "TUNE", -12.0f, 12.0f, 0.0f, "st").slow(),
                 choice ("echo_time", "ECHO TIME", divisionNames (divIndex ("1 BAR"), divIndex ("1/16")), divIndex ("1/4D") - divIndex ("1 BAR")),
                 pct ("echo_fb", "ECHO FDBK", 0.55f).slow(),
                 pct ("pocket_depth", "POCKET DEPTH", 0.2f).slow(),
                 pct ("pocket_shape", "POCKET SHAPE", 0.3f),
                 choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 1),
                 toggle ("alloc", "ALLOC", true).noPreset().forget(),
                 toggle ("key_follow", "KEY FOLLOW", true).noPreset().forget() });
    }
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP PHRASE (C3 = SLICE 1)"; }
    void loadFiles (const juce::StringArray& f) override { if (f.size() > 0) sample.set (loadSampleFile (juce::File (f[0]), sr, 300.0)); }
    void save (juce::ValueTree& t) const override { sample.save (t, "SAMPLE"); }
    void load (const juce::ValueTree& t) override { sample.load (t, "SAMPLE", sr); }

    void prepare (double rate, int mb) override
    {
        if (auto s = sample.get(); s != nullptr && std::abs (s->sr - rate) > 1 && s->flac.getSize() > 0) sample.set (loadSampleFlac (s->flac, s->name, rate, s->flacGain));
        sr = rate;
        out.prepare (rate, mb, ROLE_VOICE, "VOICE", true);
        events.reserve (2048);
        for (auto& d : echoLine) d.prepare ((int) (rate * 9));
        for (int b = 0; b < kVb; ++b)
        {
            const float f = expMap ((float) b / (float) (kVb - 1), 120.0f, 7000.0f);
            ana[b].set (f, 6.0f, rate); car[b].set (f, 6.0f, rate);
            fol[b].set (0.002f, 0.03f, rate);
        }
        os.prepare (rate, 2);
        reset();
    }
    void reset() override
    {
        out.reset();
        echoes.reset();
        play = {};
        for (auto& v : choir) { v.active = false; for (auto& f : v.fm) f.reset(); }
        for (auto& f : echoLp) f.reset();
        for (auto& d : echoLine) d.clear();
        for (int b = 0; b < kVb; ++b) { ana[b].reset(); car[b].reset(); fol[b].z = 0.0f; }
        for (auto& f : radio) f.reset();
        for (auto& f : radioHi) f.reset();
        os.reset();
        heldY = 0.0f;
    }

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midi) override
    {
        echoes.run (c, midi, events, c.v (DENS), echoSpaceBeats (c.ci (ESPACE)), 0.0f, 1.0f, true);
        const SampleData* smp = sample.audio();
        const SampleRegion reg = smp != nullptr ? regionFrom (c, SSTART, *smp) : SampleRegion();    // a note started now plays in this part
        const int model = c.ci (MODEL);
        int autoOff; float relMul;
        gateShape (c.v (GATE), c.samplesPerBeat(), autoOff, relMul);
        const float tone = c.v (TONE), sig = c.v (SIGNAL), drive = c.v (DRIVE);
        const float rate = semis (c.v (TUNE));
        keyShift = c.b (KEYFOLLOW) ? (float) keyOffset (c.keyRoot) : 0.0f;
        // Carrier pitch for VOCODER / ROBOT / CHOIR: C1..C4 from TONE (in the KEY).
        const float carrierHz = noteHz (36.0f + std::round (tone * 36.0f) + keyShift);
        // Choir formants for A E I O U, morphed by TONE (set once per chunk).
        {
            static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const float pos = tone * 4.0f;
            const int a = std::min (3, (int) pos);
            const float fr = pos - (float) a;
            for (int k = 0; k < 3; ++k) formant[k] = lerp (F[a][k], F[a + 1][k], fr);
            for (auto& v : choir)
                if (v.active)
                    for (int k = 0; k < 3; ++k) v.fm[k].set (formant[k], 8.0f, sr);
        }
        float dry[Oversampler::kBlock];
        const int nn = std::min (c.n, Oversampler::kBlock);
        const float choirCal = cal::gain (cal::voiceChoir);
        const float phraseCal = smp != nullptr ? cal::at (cal::voice, model) : 1.0f;
        for (int k = 0; k < 2; ++k) { radio[k].set (300.0f * (1.0f - sig * 0.6f), 0.7f, sr); radioHi[k].set (2200.0f + sig * 9000.0f, 0.7f, sr); }
        const double echoBeats = divisionBeats (c.ci (ECHOTIME), divIndex ("1 BAR"));
        const float echoD = (float) std::min (echoBeats * c.samplesPerBeat(), sr * 8.5);
        const float echoAmt = c.v (ECHO), echoFb = c.v (ECHOFB) * 0.95f;
        for (auto& f : echoLp) f.set (2800.0f, 0.7f, sr);
        size_t e = 0;
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        // Pass 1: the voice itself.
        for (int i = 0; i < nn; ++i)
        {
            while (e < events.size() && events[e].offset <= i)
            {
                const auto& ev = events[e++];
                if (ev.on) noteOn (ev, smp, c.ci (SLICEMODE), autoOff, relMul, reg);
                else noteOff (ev.note, ev.gen);
            }
            float x = 0.0f;
            if (smp != nullptr)
            {
                if (play.active)
                {
                    const int ip = (int) play.pos;
                    const int len = smp->length();
                    if (ip + 2 >= play.end || ip + 2 >= len) play.active = false;
                    else
                    {
                        const float a = 0.5f * (smp->read (0, play.pos, rate) + smp->read (1, play.pos, rate)) * play.reg.gain (play.pos);
                        float env = std::min (1.0f, (float) (play.pos - play.start) / (0.003f * (float) sr) + 0.001f);
                        if (play.releasing) { play.rel *= play.relCoef; env *= play.rel; if (play.rel < 1e-4f) play.active = false; }
                        if (play.autoOff > 0 && --play.autoOff == 0) startRelease (relMul);
                        x = a * env * play.vel;
                        play.pos += rate;
                        if (model == 5)
                        {
                            // STUTTER: loop the first part of the slice; TONE sets the loop length.
                            const double loop = (0.5 - tone * 0.45) * c.samplesPerBeat() * 0.5;
                            if (play.pos - play.start >= loop) play.pos = play.start;
                        }
                    }
                }
                switch (model)
                {
                    case 1: x = radioChain (x, sig); break;
                    case 2: case 3: case 4: x = vocode (x, model, carrierHz, sig); break;
                    default: break;
                }
            }
            else
                x = choirSynth() * choirCal;
            dry[i] = x * phraseCal;
        }
        // DRIVE, 2x oversampled. Always through the oversampler (no delay switching in or out);
        // below 2 % the drive fades in from exactly clean.
        {
            const float g = 1.0f + drive * 8.0f, nrm = 1.0f / std::sqrt (g), w = std::min (1.0f, drive / 0.02f);
            os.process (0, dry, nn, [g, nrm, w] (float x) { return x + w * (fastTanh (x * g) * nrm - x); });
        }
        // Pass 2: dub echo send.
        for (int i = 0; i < nn; ++i)
        {
            const float x = dry[i];
            const float yl = echoLine[0].read (echoD), yr = echoLine[1].read (echoD * 1.5f > sr * 8.5f ? echoD : echoD * 1.5f);
            echoLine[0].push (x * echoAmt + echoLp[0].tickLP (fastTanh (yr * echoFb)));
            echoLine[1].push (echoLp[1].tickLP (fastTanh (yl * echoFb)));
            L[i] = x + yl * 0.9f;
            R[i] = x + yr * 0.9f;
        }
        out.process (c, io, FILT, -1, c.v (PDEPTH), c.v (PSHAPE), c.b (ALLOCON), LEVEL);
        dispSlice = play.active ? play.slice : -1;
    }

    // The phrase: the part that plays bright, its slices (the one playing green), START and END to
    // drag over it.
    int displayHeight() const override { return 110; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "PHRASE");
        auto in = r.reduced (8, 18);
        auto s = sample.get();
        if (s == nullptr) { ui::label (g, "NO PHRASE: FORMANT CHOIR PLAYS THE NOTES", in, ui::kFaint, 11.0f, juce::Justification::centred); return; }
        const int n = std::max (1, s->length());
        const auto reg = SampleStrip::regionOf (*s, *this, SSTART);
        SampleStrip::drawRegion (g, in, *s, reg);
        const int count = Slices::count (*s, sliceModeDisp, reg);
        for (int k = 0; k < count; ++k)
        {
            const float x = in.getX() + (float) (Slices::start (*s, sliceModeDisp, k, reg) / n) * in.getWidth();
            g.setColour ((int) k == dispSlice.load() ? ui::kGreen : ui::kText);
            g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
            ui::label (g, juce::MidiMessage::getMidiNoteName (60 + (int) k, true, true, 3), { x + 2, in.getY(), 30, 12 }, ui::kFaint, 9.0f);
        }
        ui::label (g, SampleStrip::times (*s, reg), r.reduced (8, 2).removeFromTop (14), ui::kFaint, 10.0f, juce::Justification::centredRight);
        ui::label (g, s->name.toUpperCase(), r.reduced (8, 2).removeFromBottom (14), ui::kFaint, 10.0f, juce::Justification::centredRight);
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        if (sample.get() != nullptr) strip.mouse (p, r.reduced (8, 18), down, *this, SSTART);
        else if (down) strip.up();
    }
    void displayMouseUp() override { strip.up(); }

private:
    static constexpr int kVb = 16;
    struct Play { bool active = false, releasing = false; double pos = 0, start = 0; int end = 0, slice = -1, autoOff = -1; float vel = 1, rel = 1, relCoef = 0.999f; SampleRegion reg; };
    struct ChoirVoice { bool active = false; int note = 0, gen = 0; float f = 220, a = 0, target = 0; Osc osc; Svf fm[3]; };

    void noteOn (const NoteEvent& ev, const SampleData* s, int sliceMode, int autoOff, float relMul, const SampleRegion& reg)
    {
        sliceModeDisp = sliceMode;
        if (s == nullptr)
        {
            ChoirVoice* v = nullptr;
            for (auto& x : choir) if (! x.active) { v = &x; break; }
            if (v == nullptr) { v = &choir[0]; for (auto& x : choir) if (x.a < v->a) v = &x; }     // steal the quietest
            v->active = true; v->note = ev.note; v->gen = ev.gen; v->f = noteHz ((float) ev.note + keyShift); v->target = ev.vel * ev.gain; v->osc = Osc();
            for (int k = 0; k < 3; ++k) v->fm[k].set (formant[k], 8.0f, sr);
            return;
        }
        const int count = Slices::count (*s, sliceMode, reg);
        const int idx = ev.note - 60;
        play.slice = (idx >= 0 && sliceMode != 3) ? std::min (idx, count - 1) : -1;
        play.start = play.slice >= 0 ? Slices::start (*s, sliceMode, play.slice, reg) : reg.start;
        play.end = play.slice >= 0 && play.slice + 1 < count ? (int) Slices::start (*s, sliceMode, play.slice + 1, reg) : (int) reg.end;
        play.reg = reg;
        play.pos = play.start;
        play.active = true; play.releasing = false; play.rel = 1.0f;
        play.vel = (0.5f + 0.5f * ev.vel) * ev.gain;      // velocity response, then the DENSITY fade
        play.autoOff = autoOff;
        play.relCoef = std::exp (-1.0f / (0.02f * relMul * (float) sr));
    }
    void startRelease (float relMul) { play.releasing = true; play.relCoef = std::exp (-1.0f / (0.02f * relMul * (float) sr)); }
    // A played note's release ends every choir voice on that note; a DENSITY ghost's release ends
    // that ghost (ghosts that start after their note was released used to sound forever).
    void noteOff (int note, int gen)
    {
        for (auto& v : choir)
            if (v.active && (note < 0 || (v.note == note && (gen == 0 || v.gen == gen)))) v.target = 0.0f;
    }

    float radioChain (float x, float sig)
    {
        x = radioHi[0].tickLP (radio[0].tickHP (x));
        x = fastTanh (x * (2.5f - sig * 1.5f)) * 0.8f;
        return x + rng.next() * 0.01f * (1.0f - sig);
    }

    float vocode (float x, int model, float carrierHz, float sig)
    {
        // Carrier (band-limited).
        float c = 0.0f;
        if (model == 3) c = 0.52f * (carrier[0].tick (carrierHz, sr, 1, 0.04f) + 0.92f);           // ROBOT: pulse train
        else if (model == 4)
        {
            c = carrier[0].tick (carrierHz, sr, 0)                                                   // CHOIR: minor third + fifth
              + carrier[1].tick (carrierHz * 1.1892f, sr, 0) * 0.8f
              + carrier[2].tick (carrierHz * 1.4983f, sr, 0) * 0.7f;
            c *= 0.5f;
        }
        else c = carrier[0].tick (carrierHz, sr, 0);                                               // VOCODER: saw
        c += rng.next() * 0.15f * sig;
        float y = 0.0f;
        for (int b = 0; b < kVb; ++b)
        {
            const float env = fol[b].tick (ana[b].tickBP (x));
            y += car[b].tickBP (c) * env * (1.0f + (float) b / kVb * sig * 2.0f);
        }
        y *= 2.5f;
        if (model == 3)
        {
            // ROBOT: 8 kHz / 8-bit
            if (++holdN >= std::max (1, (int) (sr / 8000.0))) { holdN = 0; heldY = std::round (y * 128.0f) / 128.0f; }
            y = heldY;
        }
        return y;
    }

    float choirSynth()
    {
        // Formants are set once per chunk (process); band-limited saw source.
        float y = 0.0f;
        for (auto& v : choir)
        {
            if (! v.active) continue;
            v.a += (v.target - v.a) * (v.target > v.a ? 0.0005f : 0.0002f);
            if (v.target <= 0.0f && v.a < 1e-4f) { v.active = false; continue; }
            const float src = v.osc.tick (v.f, sr, 0) + rng.next() * 0.05f;
            float out = 0.0f;
            for (int k = 0; k < 3; ++k) out += v.fm[k].tickBP (src) * (k == 0 ? 1.0f : 0.6f);
            y += out * v.a * 0.25f;
        }
        return y;
    }

    double sr = 48000.0;
    RoleOutput out;
    EchoScheduler echoes;
    std::vector<NoteEvent> events;
    SampleSlot sample;
    Play play;
    std::array<ChoirVoice, 6> choir;
    Svf ana[kVb], car[kVb], radio[2], radioHi[2], echoLp[2];
    Follower fol[kVb];
    DelayLine echoLine[2];
    Noise rng;
    Osc carrier[3];
    Oversampler os;
    float heldY = 0, keyShift = 0, formant[3] { 800, 1150, 2900 };
    int holdN = 0, sliceModeDisp = 0;
    std::atomic<int> dispSlice { -1 };
    SampleStrip strip;                  // START and END dragged on the phrase
};
ModulePtr makeVOICE() { return std::make_unique<VoiceModule>(); }
} // namespace sf
