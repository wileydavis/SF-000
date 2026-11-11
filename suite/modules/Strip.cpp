// STRIP: the suite's performance row for any track. Goes after any Live instrument, sampler or
// hardware input. Audio only (Live sends no MIDI to effects), so DENSITY and GATE work on the
// hits it hears.
//   chain: GATE -> DENSITY -> DRIVE (4x) -> FILTER -> SPACE -> POCKET -> ALLOC -> LEVEL
#include "Kit.h"
#include "../core/Oversample.h"
#include <cstring>

namespace sf
{
static const uint16_t kStripPatterns[] = { 0xFFFF, 0xAAAA, 0x2222, 0x5555, 0x9292, 0xB6DB, 0xEEEE, 0x8888, 0x7777, 0xA94A, 0x9228, 0x9249 };
static std::vector<std::string> stripPatternNames() { return { "FULL", "8THS", "OFFBEAT", "OFF 8THS", "TRESILLO", "GALLOP", "SKIP 1", "STAB", "PUMP", "BROKEN", "CLAVE", "DOTTED" }; }

class StripModule : public Module
{
public:
    enum { DENS, GATE, FILT, DRIVE, POCKETD, SPACE, THROW, LEVEL,
           ROLE, ALLOCON, ESPACE, EPROB, DARKEN, GATEMODE, PATTERN, RATE, CHARACTER, RES, PSHAPE, SPACETYPE, SPACESIZE, SENS };
    std::string id() const override { return "STRIP"; }
    std::vector<ParamSpec> params() const override
    {
        std::vector<std::string> roles; for (int i = 0; i < kNumRoles; ++i) roles.push_back (kRoleNames[i]);
        auto role = choice ("role", "ROLE", roles, ROLE_OTHER); role.song = false; role.preset = false; role.setup = true;
        return { pct ("density", "DENSITY", 0.0f).prim().dir (1),
                 pct ("gate", "GATE", 1.0f).prim().dir (-1),
                 bip ("filter", "FILTER").prim(),
                 pct ("drive", "DRIVE", 0.0f).prim().dir (1),
                 pct ("pocket", "POCKET", 0.0f).prim(),
                 pct ("space", "SPACE", 0.0f).prim().dir (1),
                 momentary ("throw", "THROW").prim(),
                 levelParam().prim(),
                 role,
                 toggle ("alloc", "ALLOC", true).noPreset().forget(),
                 choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 1),
                 pct ("echo_prob", "ECHO PROB", 1.0f),
                 pct ("darken", "DARKEN", 0.4f),
                 choice ("gate_mode", "GATE MODE", { "HITS", "PATTERN" }, 0),
                 choice ("pattern", "PATTERN", stripPatternNames(), 1),
                 choice ("rate", "RATE", { "1/8", "1/16", "1/32" }, 1),
                 pct ("character", "CHARACTER", 0.0f),
                 pct ("res", "FILTER RES", 0.2f),
                 pct ("pocket_shape", "POCKET SHAPE", 0.3f),
                 choice ("space_type", "SPACE TYPE", { "REVERB", "DUB ECHO" }, 1),
                 pct ("space_size", "SPACE SIZE", 0.5f),
                 pct ("sensitivity", "SENSITIVITY", 0.5f) };
    }
    int latency() const override { return Pocket::kLatency; }
    bool takesDropCut (const Ctx& c) const override { return c.ci (ROLE) != ROLE_DECK; }   // a source, as an instrument
    double tailSeconds() const override { return 10.0; }

    void prepare (double rate, int mb) override
    {
        sr = rate;
        for (auto& d : dline) d.prepare ((int) (rate * 4.5));
        for (auto& d : passLine) d.prepare ((int) (rate * 4.5));
        for (auto& d : echo) d.prepare ((int) (rate * 3.0));
        for (auto& t : taps) for (auto& f : t.lp) f.reset();
        for (auto& f : echoLp) f.reset();
        filt.prepare (rate);
        fdn.prepare (rate);
        pocket.prepare (rate, mb);
        tap.prepare (rate);
        fast.set (0.0005f, 0.01f, rate); slow.set (0.01f, 0.25f, rate);
        os.prepare (rate, 4);
        reset();
    }
    void reset() override
    {
        for (auto& d : dline) d.clear();
        for (auto& d : passLine) d.clear();
        for (auto& d : echo) d.clear();
        for (auto& tp : taps) { for (auto& f : tp.lp) f.reset(); tp.g = 1.0f; }
        for (auto& f : echoLp) f.reset();
        filt.reset(); fdn.clear(); pocket.reset(); tap.reset(); os.reset();
        fast.z = slow.z = 0.0f;
        gateG = 1.0f; holdLeft = 0; sinceOnset = 1 << 20; throwG = 0.0f; spaceTail = 0;
    }

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = c.n;
        const int role = c.ci (ROLE);
        if (role != lastRole)
        {
            lastRole = role;
            char nm[24] = "STRIP ";
            std::strncat (nm, kRoleNames[juce::jlimit (0, kNumRoles - 1, role)], 16);
            tap.setIdentity (role, nm);
        }
        pocket.dropDuck = dropDucks (role);
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);

        // ---- onsets (shared by GATE HITS and DENSITY PROB)
        const float thresh = 1.4f + (1.0f - c.v (SENS)) * 1.6f;
        const float floorLvl = 0.0005f + (1.0f - c.v (SENS)) * 0.01f;

        // ---- GATE
        const float g = c.v (GATE);
        const bool pattern = c.ci (GATEMODE) == 1;
        const bool open = g >= 0.97f;
        // HITS: each detected hit stays open for HOLD, then closes over 5 ms.
        // 0 % = 1/64 beat, 97 %+ = never closes. PATTERN: GATE = how long each step stays open.
        const double holdBeats = 0.015 + std::pow (g / 0.97, 2.0) * 1.985;
        const int holdSamples = (int) (holdBeats * c.samplesPerBeat());
        static const double rates[] = { 0.5, 0.25, 0.125 };
        const double stepB = rates[juce::jlimit (0, 2, c.ci (RATE))];
        const uint16_t pat = kStripPatterns[(size_t) juce::jlimit (0, 11, c.ci (PATTERN))];
        const double late = groove.beats (*c.bus, c.bpm);     // the pattern on the groove's grid (KickGroove)
        const float att = 1.0f / (0.001f * (float) sr), rel = 1.0f / (0.005f * (float) sr);

        // ---- DENSITY (audio ghost echoes, as SF DENSITY)
        const double space = echoSpaceBeats (c.ci (ESPACE)) * c.samplesPerBeat();
        const float d = c.v (DENS);
        const float t1 = clamp01 (d * 2.0f), t2 = clamp01 ((d - 0.5f) * 2.0f);
        const float delays[3] = { (float) space, (float) (space * 0.5), (float) (space * 1.5) };
        const float levels[3] = { 0.62f * t1, 0.38f * t2, 0.32f * t2 };
        const int tiers[3] = { 1, 2, 2 };
        const float dark = c.v (DARKEN), prob = c.v (EPROB);
        for (int k = 0; k < 3; ++k)
            for (int ch = 0; ch < 2; ++ch)
                taps[k].lp[ch].set (expMap (1.0f - dark * (tiers[k] == 1 ? 0.6f : 0.85f), 600.0f, 18000.0f), 0.7071f, sr);
        const float gateSmooth = std::exp (-1.0f / (float) (0.001 * sr));

        // ---- DRIVE
        const float drive = c.v (DRIVE), ch = c.v (CHARACTER);
        const float pre = dbToGain (drive * 30.0f);

        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i], m = 0.5f * (l + r);
            const float f = fast.tick (m), s = slow.tick (m);
            bool onset = false;
            if (f > s * thresh && f > floorLvl && sinceOnset > (int) (0.03 * sr)) { onset = true; sinceOnset = 0; pass1 = rng.uni() <= prob ? 1.0f : 0.0f; pass2 = rng.uni() <= prob ? 1.0f : 0.0f; }
            ++sinceOnset;

            float target = 1.0f;
            if (pattern)
            {
                const double pos = (c.ppqAt (i) - late) / stepB;
                const int step = (int) (((long long) std::floor (pos)) % 16 + 16) % 16;
                const float frac = (float) (pos - std::floor (pos));
                const bool on = (pat >> (15 - step)) & 1;
                target = (on && frac < 0.05f + 0.95f * g) ? 1.0f : 0.0f;
            }
            else if (! open)
            {
                if (onset) holdLeft = holdSamples;
                target = holdLeft > 0 ? 1.0f : 0.0f;
                if (holdLeft > 0) --holdLeft;
            }
            gateG = target > gateG ? std::min (target, gateG + att) : std::max (target, gateG - rel);
            const float gl = l * gateG, gr = r * gateG;

            dline[0].push (gl); dline[1].push (gr);
            passLine[0].push (pass1); passLine[1].push (pass2);
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                if (levels[k] <= 0.0005f) continue;
                const float gp = passLine[tiers[k] - 1].tap ((int) delays[k]);
                taps[k].g = gp + gateSmooth * (taps[k].g - gp);
                const float a = levels[k] * taps[k].g;
                wl += taps[k].lp[0].tickLP (dline[0].read (delays[k])) * a;
                wr += taps[k].lp[1].tickLP (dline[1].read (delays[k])) * a;
            }
            L[i] = gl + wl; R[i] = gr + wr;
        }
        // DRIVE, 4x oversampled: TAPE (soft) .. FUZZ (hard) by CHARACTER. Always through the
        // oversampler (no delay switching in or out) and blended in from exactly clean at 0.
        {
            const float comp = 1.6f / (1.0f + drive * 2.5f);
            auto shape = [ch, pre, comp, drive] (float x)
            {
                const float y = lerp (fastTanh (x * pre), hardClip (x * pre * 2.5f, 0.8f), ch) * comp;
                return x + drive * (y - x);
            };
            os.process (0, L, n, shape);
            os.process (1, R, n, shape);
        }

        // ---- FILTER
        filt.process (io.getArrayOfWritePointers(), 2, n, c.v0 (FILT), c.v (FILT), c.v (RES));

        // ---- SPACE (send) + THROW
        const float sendAmt = c.v (SPACE);
        const bool throwOn = c.b (THROW);
        const bool reverb = c.ci (SPACETYPE) == 0;
        const float size = c.v (SPACESIZE);
        if (reverb) fdn.set (0.3f + size * 0.6f, 1.0f + size * 7.0f, 0.5f, 0.3f, false, n);
        const float echoD = (float) ((0.5 + size * 0.25) * c.samplesPerBeat());   // 1/8 .. 3/16
        const float echoFb = 0.35f + size * 0.5f;
        for (auto& lp : echoLp) lp.set (2800.0f, 0.7071f, sr);
        for (int i = 0; i < n; ++i)
        {
            throwG += ((throwOn ? 1.0f : 0.0f) - throwG) * 0.004f;
            const float send = std::min (1.0f, sendAmt + throwG);
            const float il = L[i] * send, ir = R[i] * send;
            float ol = 0, orr = 0;
            if (reverb) { if (send > 0.0005f || spaceTail > 0) fdn.tick (il, ir, ol, orr, false); }
            else
            {
                const float yl = echo[0].read (echoD), yr = echo[1].read (echoD);
                echo[0].push (il + echoLp[1].tickLP (fastTanh (yr * echoFb)));   // ping-pong
                echo[1].push (ir * 0.3f + echoLp[0].tickLP (fastTanh (yl * echoFb)));
                ol = yl; orr = yr;
            }
            L[i] += ol; R[i] += orr;
        }
        // The reverb rings out after SPACE / THROW close (its RT60 to -96 dB), never cut.
        spaceTail = sendAmt > 0.0005f || throwG > 0.0005f ? (int) ((1.0f + size * 7.0f) * 1.6f * (float) sr) : std::max (0, spaceTail - n);

        // ---- POCKET, ALLOC, LEVEL
        pocket.process (c, io.getArrayOfWritePointers(), 2, n, c.v (POCKETD), c.v (PSHAPE), nullptr, c.v (POCKETD) > 0.001f);
        tap.process (io.getArrayOfWritePointers(), 2, n, c.b (ALLOCON), &c);
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
        dispGate = gateG; dispDens = d;
    }

    int displayHeight() const override { return 80; }
    void paint (juce::Graphics& gr, juce::Rectangle<float> r) override
    {
        auto left = r.removeFromLeft (r.getWidth() * 0.4f);
        ui::frame (gr, left, "GATE / DENSITY");
        auto in = left.reduced (10, 20);
        ui::label (gr, "GATE", in.removeFromTop (14), ui::kFaint, 10.0f);
        ui::valueBar (gr, in.removeFromTop (8), dispGate.load());
        in.removeFromTop (8);
        ui::label (gr, "DENSITY", in.removeFromTop (14), ui::kFaint, 10.0f);
        ui::valueBar (gr, in.removeFromTop (8), dispDens.load());
        pocket.paint (gr, r.withTrimmedLeft (8));
    }

private:
    double sr = 48000.0;
    DelayLine dline[2], passLine[2], echo[2];
    struct Tap { Svf lp[2]; float g = 1.0f; } taps[3];
    Svf echoLp[2];
    DJFilter filt;
    Oversampler os;
    Fdn fdn;
    Pocket pocket;
    AllocTap tap;
    Follower fast, slow;
    Noise rng;
    int holdLeft = 0, sinceOnset = 1 << 20, lastRole = -1;
    float gateG = 1.0f, pass1 = 1.0f, pass2 = 1.0f, throwG = 0.0f;
    int spaceTail = 0;
    std::atomic<float> dispGate { 1.0f }, dispDens { 0.0f };
    KickGroove groove;                    // where the groove lands against Live's beat
};
ModulePtr makeSTRIP() { return std::make_unique<StripModule>(); }
} // namespace sf
