// KICK, PERC, RUMBLE
// KICK and PERC voices render at 2x the host rate (band-limited squares for the metallic
// sources, drive inside the oversampled path) and are decimated by a halfband filter; RUMBLE's
// GRIT runs 2x oversampled. Sample models read with 4-point Hermite interpolation.
#include "Kit.h"
#include "../core/BandLimited.h"
#include "../core/Oversample.h"
#include "Calibration.h"

namespace sf
{
// (SF-000) DECAY as a gate on samples (sampleGateSeconds, sampleGateGain): in Kit.h now, for the loop there.

// =====================================================================================  KICK
// KICK 909-TYPE and KICK 808-TYPE are their circuits [Nov 10] (renderVoice: kick909, kick808); the
// other models are one pitch-swept body with a click. For the circuit models the table gives the
// resting pitch (f1), the sweep's start against it (ratio: 909) and time constant (tau), and the
// decay's time constant; the rest of their rows is unused.
struct KickModel { const char* name; float f1, ratio, tau, decay, click, clickHz, drive, shape, lp, fm, gain; };
static const KickModel kKickModels[] = {
    { "KICK 909-TYPE",    50.0f, 4.5f, 0.009f, 0.35f, 0.60f, 3500.0f, 0.10f, 0.00f, 20000.0f, 0.00f, 1.0f },
    { "KICK 808-TYPE",    49.5f, 2.2f, 0.002f, 0.45f, 0.08f, 2000.0f, 0.02f, 0.00f, 20000.0f, 0.00f, 1.0f },
    { "KICK SINE",        48.0f, 3.0f, 0.012f, 0.50f, 0.05f, 3000.0f, 0.00f, 0.00f, 12000.0f, 0.00f, 0.62f },
    { "KICK INDUSTRIAL",  52.0f, 7.0f, 0.006f, 0.45f, 0.70f, 4500.0f, 0.85f, 0.30f,  9000.0f, 0.00f, 0.6f },
    { "KICK MODULAR",     55.0f, 4.0f, 0.015f, 0.30f, 0.30f, 2500.0f, 0.30f, 0.60f,  3000.0f, 0.35f, 0.72f },
    { "KICK DUB",         44.0f, 2.0f, 0.025f, 0.60f, 0.00f, 2000.0f, 0.10f, 0.00f,   600.0f, 0.00f, 0.8f },
    { "SAMPLE",           50.0f, 1.0f, 0.010f, 0.50f, 0.00f, 2000.0f, 0.00f, 0.00f, 20000.0f, 0.00f, 1.0f },
    // (SF-000) KICK FORGE (see ForgeShape, and forgeVoice): of this row only the resting pitch (G1, where a key moves it
    // from) and the gain are read. After SAMPLE: a model's number never moves.
    { "KICK FORGE",       49.0f, 3.0f, 0.012f, 0.40f, 0.00f, 2000.0f, 0.00f, 0.00f, 20000.0f, 0.00f, 1.0f } };
static constexpr int kNumKickModels = 8;
static constexpr int kKickSample = 6, kKickForge = 7;

// (SF-000) KICK FORGE. One kick made of three parts that are tied to each other, and one knob, CHARACTER, that takes all
// of them through four kicks with no step between: mellow and dubby, punchy and tight, boomy, distorted.
//   IMPACT  the front of the hit, up to nine hundredths of a second dying away: a piece of the lane's own file (a field
//           recording: a door, a stone, a tool), from POSITION, with its lows under the body taken out and brought to one
//           strength wherever it is from, so that another place in the file is another thing struck. With no file, a
//           burst of noise in a band POSITION places. This is the one part that is not the same hit after hit.
//   BODY    a sine that starts at the same point of its wave every hit (the low end is the same every time), falling from
//           above onto the kick's note (the key's root with KEY FOLLOW); for its first thirtieth of a second it bends
//           its own pitch with its own output, which is where the knock is; harmonics that die sooner than the
//           fundamental; its lows (under 120 Hz) kept clean and its mids driven
//   GROUND  the body's own tail, held in a small room of delays under 150 Hz, rung at the kick's note, and held down
//           while the body sounds: what is heard between the kicks
// and a soft clipper over the impact and the body, which is what makes them one sound when driven.
// What CHARACTER sets, at its four kicks (in between: a smooth curve through them). The lane's own knobs move from there.
struct ForgeShape { float ratio, tau, decay, fm, harm, drive, impact, impTau, impHp, ground, groundT, lp, clip, sub; };
static const ForgeShape kForge[4] = {
    //  ratio  tau     decay  fm    harm  drive impact impTau  impHp   ground groundT lp       clip  sub
    {   1.7f,  0.034f, 0.170f, 0.00f, 0.00f, 0.00f, 0.25f, 0.012f, 1200.0f, 0.45f, 0.55f,   700.0f, 0.8f, 1.00f },    // mellow, dubby
    {   5.2f,  0.007f, 0.055f, 0.55f, 0.22f, 0.20f, 0.90f, 0.022f,  500.0f, 0.18f, 0.25f, 14000.0f, 1.2f, 0.90f },    // punchy, tight
    {   3.0f,  0.018f, 0.240f, 0.25f, 0.12f, 0.30f, 0.65f, 0.035f,  350.0f, 0.90f, 0.95f,  5000.0f, 1.4f, 1.15f },    // boomy
    {   6.5f,  0.011f, 0.130f, 1.30f, 0.80f, 1.00f, 1.00f, 0.045f,  250.0f, 0.60f, 0.70f, 16000.0f, 2.2f, 1.00f } };  // distorted
// The shape at a place on CHARACTER: a curve through the four that passes each of them (Catmull-Rom), so the way from
// one to the next has no corner in it. Never below a tenth of the smallest of them (a curve can dip under its points).
static ForgeShape forgeAt (float c)
{
    const float x = std::min (std::max (c, 0.0f), 1.0f) * 3.0f;
    const int i = std::min (2, (int) x);
    const float t = x - (float) i;
    const ForgeShape& p0 = kForge[std::max (0, i - 1)], & p1 = kForge[i], & p2 = kForge[i + 1], & p3 = kForge[std::min (3, i + 2)];
    auto cr = [t] (float a, float b, float c2, float d)
    {
        const float v = 0.5f * ((2.0f * b) + (-a + c2) * t + (2.0f * a - 5.0f * b + 4.0f * c2 - d) * t * t + (-a + 3.0f * b - 3.0f * c2 + d) * t * t * t);
        const float lo = std::min (b, c2), hi = std::max (b, c2), over = 0.15f * (hi - lo);      // (a curve may pass a little beyond its two points; never below nought)
        return std::min (std::max (v, std::max (0.0f, lo - over)), hi + over);
    };
    ForgeShape o;
    o.ratio = cr (p0.ratio, p1.ratio, p2.ratio, p3.ratio);          o.tau = cr (p0.tau, p1.tau, p2.tau, p3.tau);
    o.decay = cr (p0.decay, p1.decay, p2.decay, p3.decay);          o.fm = cr (p0.fm, p1.fm, p2.fm, p3.fm);
    o.harm = cr (p0.harm, p1.harm, p2.harm, p3.harm);               o.drive = cr (p0.drive, p1.drive, p2.drive, p3.drive);
    o.impact = cr (p0.impact, p1.impact, p2.impact, p3.impact);     o.impTau = cr (p0.impTau, p1.impTau, p2.impTau, p3.impTau);
    o.impHp = cr (p0.impHp, p1.impHp, p2.impHp, p3.impHp);          o.ground = cr (p0.ground, p1.ground, p2.ground, p3.ground);
    o.groundT = cr (p0.groundT, p1.groundT, p2.groundT, p3.groundT);
    o.lp = std::exp (cr (std::log (p0.lp), std::log (p1.lp), std::log (p2.lp), std::log (p3.lp)));
    o.clip = cr (p0.clip, p1.clip, p2.clip, p3.clip);               o.sub = cr (p0.sub, p1.sub, p2.sub, p3.sub);
    return o;
}
// CHARACTER holds its loudness: what each place on it is turned by, in dB, so that the kick is as loud at every one of
// them, and 2 dB over KICK 909-TYPE alone (measured by SF-000's check of it, forgeTest, which prints the kick's loudest
// tenth of a second at each of eleven places with the kick alone and under the master's limiter; straight lines between).
static const float kForgeTrim[11] = { 0.0f, -0.1f, 0.7f, 2.0f, 0.6f, -1.2f, -2.1f, -2.5f, -2.4f, -2.2f, -2.1f };
static float forgeTrim (float c)
{
    const float x = std::min (std::max (c, 0.0f), 1.0f) * 10.0f;
    const int i = std::min (9, (int) x);
    return std::pow (10.0f, (kForgeTrim[i] + (kForgeTrim[i + 1] - kForgeTrim[i]) * (x - (float) i)) / 20.0f);
}
static std::vector<std::string> kickModelNames() { std::vector<std::string> v; for (auto& m : kKickModels) v.push_back (m.name); return v; }

class KickModule : public Module
{
public:
    // SSTART: the sample's START; END, FADE IN, FADE OUT follow (sampleRegionParams); then
    // SPLIT and SPLIT HZ [Nov 10] (ids are appended, never moved).
    enum { TUNE, DECAY, PUNCH, CLICK, DRIVE, FILT, DENS, LEVEL, MODEL, LAYERMODEL, LAYER, PTIME, VELSENS, ESPACE, ESWING, KEYTRACK, RES, ALLOCON, KEYFOLLOW, SSTART,
           SPLIT = SSTART + 4, SPLITHZ, SGAIN, SNORM, SSLICE, SCYCLE,         // (SF-000) the sample's own GAIN and NORMALISE, then SLICE and CYCLE
           FGCHAR, FGIMPACT, FGBODY, FGGROUND, FGPOS, FGTONE,                 // (SF-000) KICK FORGE's own
           SCUTS, SCUTLAST = SCUTS + kCuts };                                 // (SF-000) MARKERS and the 32 slice markers, last
    std::string id() const override { return "KICK"; }
    bool instrument() const override { return true; }
    std::vector<ParamSpec> params() const override
    {
        auto p = withRegion ({ knob ("tune", "TUNE", -12.0f, 12.0f, 0.0f, "st").prim(),
                 pct ("decay", "DECAY", 0.5f).prim().dir (1),
                 pct ("punch", "PUNCH", 0.5f).prim().dir (1),
                 pct ("click", "CLICK", 0.5f).prim(),
                 pct ("drive", "DRIVE", 0.2f).prim().dir (1),
                 bip ("filter", "FILTER").prim(),
                 pct ("density", "DENSITY", 0.0f).prim(),
                 levelParam (-3.0f).prim(),
                 choice ("model", "MODEL", kickModelNames(), 0),
                 choice ("layer_model", "LAYER MODEL", kickModelNames(), 1).layerGroup(),
                 pct ("layer", "LAYER", 0.0f).slow().layerGroup(),
                 pct ("pitch_time", "PITCH TIME", 0.5f).slow(),
                 pct ("vel_sens", "VEL SENS", 0.5f),
                 choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 1),
                 pct ("echo_swing", "ECHO SWING", 0.0f).slow(),
                 toggle ("key_track", "KEY TRACK"),
                 pct ("res", "FILTER RES", 0.1f),
                 toggle ("alloc", "ALLOC", true).noPreset().forget(),
                 toggle ("key_follow", "KEY FOLLOW", true).noPreset().forget() });
        // LAYER SPLIT (Kit.h): one kick's lows, the other's punch and click.
        p.push_back (knob ("split", "SPLIT", 0.0f, 1.0f, 0.5f, "SPLIT").layerGroup());
        p.push_back (knob ("split_hz", "SPLIT HZ", 50.0f, 800.0f, 150.0f, "Hz", 200.0f).layerGroup());
        p = withSlices (withSampleGain (std::move (p)));    // (SF-000)
        // (SF-000) KICK FORGE's own. CHARACTER: mellow and dubby, through punchy and tight and boomy, to distorted. IMPACT,
        // BODY and GROUND: how much of each part, each at its middle what CHARACTER makes it (none, to twice that).
        // POSITION: where in the lane's file the impact is taken from. TONE: darker and plainer to the left, brighter and
        // richer in harmonics to the right. The lane's TUNE, DECAY and DRIVE act on it; PUNCH (how far the pitch falls),
        // PITCH TIME (how fast) and CLICK (a pulse on the front of it) move it from where CHARACTER has them.
        p.push_back (pct ("fg_char", "CHARACTER", 0.3f).slow().engineGroup());
        p.push_back (pct ("fg_impact", "IMPACT", 0.5f).engineGroup());
        p.push_back (pct ("fg_body", "BODY", 0.5f).engineGroup());
        p.push_back (pct ("fg_ground", "GROUND", 0.5f).slow().engineGroup());
        p.push_back (pct ("fg_pos", "POSITION", 0.1f).engineGroup());
        p.push_back (pct ("fg_tone", "TONE", 0.5f).slow().engineGroup());
        p = withCuts (std::move (p));        // (SF-000) the slice markers
        jassert ((int) p.size() == SCUTLAST + 1);
        return p;
    }
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP A SAMPLE: FOR SAMPLE, OR FORGE'S IMPACT"; }
    void loadFiles (const juce::StringArray& f) override { if (f.size() > 0) sample.set (loadSampleFile (juce::File (f[0]), sr)); }
    void save (juce::ValueTree& t) const override { sample.save (t, "SAMPLE"); }
    void load (const juce::ValueTree& t) override { sample.load (t, "SAMPLE", sr); }

    void prepare (double rate, int mb) override
    {
        if (auto s = sample.get(); s != nullptr && std::abs (s->sr - rate) > 1 && s->flac.getSize() > 0) sample.set (loadSampleFlac (s->flac, s->name, rate, s->flacGain));
        sr = rate;
        osr = 2.0 * rate;
        dec.set (HalfbandDesign::forStage (rate, 0));
        decB.set (HalfbandDesign::forStage (rate, 0));
        decS.set (HalfbandDesign::forStage (rate, 0)); decSB.set (HalfbandDesign::forStage (rate, 0));      // (SF-000)
        split.prepare (rate);
        ground.prepare (osr);               // (SF-000)
        out.prepare (rate, mb, ROLE_KICK, "KICK", false);
        events.reserve (2048);
        reset();
    }
    void reset() override
    {
        echoes.reset();
        for (auto& v : voices) v.active = false;
        dec.reset();
        decB.reset();
        decS.reset(); decSB.reset(); sideLive = 0; wasWide = false;     // (SF-000)
        split.reset();
        ground.reset();
        out.reset();
    }

    // (SF-000) KICK FORGE's GROUND: the body's tail in four delays that feed each other (41 to 80 ms, each darker as it
    // goes round, nothing over 150 Hz going in), then rung at the kick's note, and held down while the body sounds. The
    // module's, not a hit's: it rings on from one kick into the next. Asleep, it costs nothing.
    struct Ground
    {
        std::vector<float> d[4];
        int len[4] = { 1, 1, 1, 1 }, w[4] = {}, live = 0;
        float z[4] = {}, inLp = 0, inK = 0, damp = 0, fb = 0.4f, level = 0, env = 0, rel = 0, dcX = 0, dcY = 0, dcR = 0;
        Svf ring;
        double rate = 96000.0;
        void prepare (double r)
        {
            rate = r;
            static const double ms[4] = { 41.3, 53.7, 67.1, 79.9 };
            for (int i = 0; i < 4; ++i) { len[i] = std::max (8, (int) (ms[i] * 0.001 * r)); d[i].assign ((size_t) len[i], 0.0f); }
            inK = 1.0f - std::exp (-kTwoPi * 150.0f / (float) r);
            damp = 1.0f - std::exp (-kTwoPi * 420.0f / (float) r);
            rel = std::exp (-1.0f / (0.07f * (float) r));
            dcR = 1.0f - kTwoPi * 28.0f / (float) r;
            reset();
        }
        void reset()
        {
            for (auto& x : d) std::fill (x.begin(), x.end(), 0.0f);
            for (auto& x : z) x = 0.0f;
            inLp = env = dcX = dcY = 0.0f; live = 0;
            for (auto& x : w) x = 0;
            ring.ic1 = ring.ic2 = 0.0f;
        }
        // A hit: how long it rings (seconds to 60 dB down), how much of it, and the note it is rung at.
        void hit (float seconds, float amount, float hz)
        {
            fb = std::pow (10.0f, -3.0f * 0.0605f / std::max (0.08f, seconds));
            level = amount;
            ring.set (hz, 5.0f, rate);
            live = (int) ((2.0 * (double) seconds + 1.0) * rate);
        }
        bool awake() const { return live > 0; }
        // One sample: in, the body as it is before its clipper.
        float tick (float in)
        {
            if (live <= 0) return 0.0f;
            if (--live == 0) { reset(); return 0.0f; }
            env = std::max (std::abs (in), env * rel);
            inLp += inK * (in - inLp);
            float o[4], sum = 0.0f;
            for (int i = 0; i < 4; ++i) { o[i] = d[i][(size_t) w[i]]; sum += o[i]; }
            for (int i = 0; i < 4; ++i)
            {
                z[i] += damp * ((o[i] - 0.5f * sum) - z[i]);          // (each back into the others: a Householder turn; darker each time round)
                d[i][(size_t) w[i]] = inLp * 0.25f + fb * z[i] + 1.0e-20f;
                if (++w[i] >= len[i]) w[i] = 0;
            }
            ring.tick (sum);
            const float y = 0.8f * sum + 2.0f * ring.bp;
            dcY = y - dcX + dcR * dcY; dcX = y;
            const float open = 1.0f - std::min (1.0f, env * 1.6f);
            return dcY * level * open * open;
        }
    };

    struct Voice
    {
        bool active = false;
        int model = 0, layer = 0;           // layer: 0 MODEL's, 1 the LAYER MODEL's (LAYER SPLIT)
        double t = 0.0, phase = 0.0;
        float vel = 1, f0 = 200, f1 = 50, tau = 0.01f, decay = 0.3f, click = 0, clickHz = 3000, drive = 0, shape = 0, lp = 8000, fm = 0;
        float fade = 1.0f, fadeStep = 0.0f, cal = 1.0f, mix = 1.0f;
        float lpZ = 0;
        unsigned tick = 0;
        Svf res;
        float srate = 1.0f, gate = 1.0e9f;
        SampleRegion reg;                   // SAMPLE: the part of the sample this hit plays
        // 909 and 808 circuits: accent (0..1), the 808's bridged-T, pulse shaper, tone low-pass and
        // the retrigger, the 909's click filters; both a DC blocker at the output.
        float accent = 1.0f, shift = 1.2f, sigh = 0.2f, sighTau = 0.1f, toneK = 1.0f, clickNz = 0.0f, clickLp = 0.0f, dcX = 0.0f, dcY = 0.0f, toneZ = 0.0f;
        Svf bridgedT, clickBp;
        // (SF-000) KICK FORGE: the fundamental's last value (it bends itself), harmonics, the body's and the impact's level,
        // the sub's, the split's state and pace, the impact's place in the file, its two high-passes, its strength, how long
        // it lasts, the clipper's drive and what it is turned by after, and whether it feeds the ground
        float fgPrev = 0, fgHarm = 0, fgBody = 1, fgImp = 0, fgSub = 1, fgLo = 0, fgLoK = 0, fgHpK = 0, fgHp1 = 0, fgHp2 = 0, fgImpTau = 0.006f, fgClip = 1, fgOut = 1, fgBright = 0;
        double fgPos = -1.0;
        float fgNoise = 1.0f;             // with no file: what the impact's noise, through its band, is turned by to be one strength
        // (SF-000) a stereo file's impact: its left less its right (halved) has the high-passes and the tone's low-pass of
        // its own, and takes the gain DRIVE and the clipper give the sum at each moment
        float fgHpS1 = 0, fgHpS2 = 0; Svf resS; bool fgWide = false;
        bool fgFeeds = false;
        float level() const { return active ? vel * std::exp (-(float) t / std::max (0.01f, decay)) : 0.0f; }
    };

    // Fade every ringing voice over 3 ms (called once per hit, before the main and the layer voice start).
    void fadeRinging()
    {
        for (auto& v : voices) if (v.active && v.fadeStep == 0.0f) v.fadeStep = 1.0f / (0.003f * (float) osr);
    }

    void trigger (Ctx& c, int model, float vel, int gen, int note, float layerGain, bool publish, int offset, const SampleData* smp, float fade)
    {
        // A free voice; else the quietest one that is already fading; else the quietest (never a
        // voice that is not fading while a fading one exists, e.g. the main voice of this same
        // hit when the layer starts).
        int idx = -1;
        for (int i = 0; i < kVoices; ++i) if (! voices[(size_t) i].active) { idx = i; break; }
        if (idx < 0)
        {
            float lowest = 1.0e9f;
            for (int i = 0; i < kVoices; ++i)
            {
                const auto& w = voices[(size_t) i];
                if (w.fadeStep > 0.0f && w.fade * w.level() * w.mix < lowest) { lowest = w.fade * w.level() * w.mix; idx = i; }
            }
        }
        if (idx < 0)
        {
            float lowest = 1.0e9f;
            for (int i = 0; i < kVoices; ++i) if (voices[(size_t) i].level() * voices[(size_t) i].mix < lowest) { lowest = voices[(size_t) i].level() * voices[(size_t) i].mix; idx = i; }
        }
        if (idx < 0) idx = 0;
        auto& v = voices[(size_t) idx];
        const auto& m = kKickModels[model];
        float tune = c.v (TUNE) + c.bend + (c.b (KEYTRACK) ? (float) (note - 36) : 0.0f);       // (SF-000: and the pitch wheel, as each hit starts)
        // KEY: the body lands on the SF CONTROL key root (nearest), TUNE moves from there.
        if (c.b (KEYFOLLOW) && c.keyRoot >= 0)
        {
            const float hz = model == kKickSample ? (smp != nullptr && smp->rootHz > 20.0f && smp->rootHz < 200.0f ? smp->rootHz : 0.0f) : m.f1;
            if (hz > 0.0f) tune += keySnap (hz, c.keyRoot);
        }
        const float decScale = std::pow (2.0f, (c.v (DECAY) - 0.5f) * 4.0f) * std::pow (0.6f, (float) gen);
        v = Voice();
        v.active = true; v.model = model;
        v.layer = publish ? 0 : 1;          // the main voice publishes the kick; the layer's doesn't
        v.vel = vel * m.gain;               // drives the voice's saturation
        v.mix = layerGain;                  // main / layer balance, applied after it
        v.cal = cal::at (cal::kick, model);
        v.f1 = m.f1 * semis (tune);
        v.f0 = v.f1 * (1.0f + (m.ratio - 1.0f) * std::pow (2.0f, (c.v (PUNCH) - 0.5f) * 2.0f));
        v.tau = m.tau * std::pow (2.0f, (c.v (PTIME) - 0.5f) * 3.0f);
        v.decay = m.decay * decScale;
        v.click = m.click * c.v (CLICK) * 2.0f;
        v.clickHz = m.clickHz;
        v.drive = clamp01 (m.drive + c.v (DRIVE) * 0.8f);
        v.shape = m.shape; v.fm = m.fm;
        v.lp = m.lp * std::pow (0.6f, (float) gen);
        v.srate = semis (tune);
        // The circuits: velocity is the accent (the 808's trigger pulse 4-14 V; the 909's envelope hit
        // harder), on top of VEL SENS's level.
        v.accent = clamp01 (vel);
        if (model == 0)                     // 909: the sweep's start, a little higher on an accent
            v.f0 = v.f1 * (1.0f + (m.ratio - 1.0f) * std::pow (2.0f, (c.v (PUNCH) - 0.5f) * 1.6f) * (0.85f + 0.3f * v.accent));
        if (model == 1)
        {
            // 808: the attack's jump (PUNCH: a little under an octave to an octave and a half over the
            // resting pitch, "more than an octave" as built), the sigh (about 20 % at a full accent,
            // PITCH TIME its pace), TONE's low-pass (CLICK: 120 Hz to 7 kHz).
            v.shift = 0.8f + 1.0f * c.v (PUNCH);
            v.f0 = v.f1 * (1.0f + v.shift);
            v.sigh = 0.22f * (0.35f + 0.65f * v.accent);
            v.sighTau = 0.1f * std::pow (2.0f, (c.v (PTIME) - 0.5f) * 2.0f);
            v.toneK = 1.0f - std::exp (-kTwoPi * 120.0f * std::pow (58.0f, c.v (CLICK)) / (float) osr);
        }
        if (model == 0) { v.clickBp.set (4500.0f, 0.9f, osr); }
        if (model == kKickForge) forgeStart (c, v, vel, gen, smp, publish);          // (SF-000)
        if (model == kKickSample)
            if (auto s = smp)
            {
                // From START; DECAY's gate measures the part that plays.
                const Cuts cuts = cutsFrom (c, SCUTS, *s);
                v.reg = hitRegion (c, *s, regionFrom (c, SSTART, *s, SGAIN), SSLICE, sliceHits++, sliceRng.uni(), &cuts);       // (SF-000) the whole of it, or a slice
                v.phase = v.reg.start;
                v.gate = sampleGateSeconds (c.v (DECAY), (float) (v.reg.length() / s->sr) / v.srate, gen);
            }
        if (publish)
        {
            float hp, lp;
            DJFilter::corners (c.v (FILT), hp, lp);
            float f1r = v.f1, f0r = v.f0, decr = v.decay;
            if (model == kKickSample)
                if (auto s = smp)
                {
                    f1r = s->rootHz > 20 && s->rootHz < 200 ? s->rootHz * v.srate : 50.0f; f0r = f1r * 2.0f; decr = std::max (0.02f, std::min (s->decaySec * 0.4f, v.gate));
                    // A cut sample is over at END, whatever its own decay.
                    if (v.reg.start > 0.0 || v.reg.end < (double) s->length()) decr = std::max (0.02f, std::min (decr, (float) (v.reg.length() / s->sr) / v.srate));
                }
            // The hit's velocity (not the voice's mix gain): layering must not make pockets shallower.
            // None for a kick silent in SF BUILD's cut: nothing ducks to it (the returns ring on).
            if (! c.inCut (c.ppqAt (offset)))
                c.bus->publishKick ({ c.ppqAt (offset), std::min (1.0f, vel * fade), f0r, f1r, v.tau, std::min (decr, lp < 200.0f ? 0.05f : decr), hp, lp });
        }
    }

    // TR-909 bass drum. A triangle VCO, reset at the trigger to the middle of its rise, swept from f0
    // down to f1 (PITCH TIME: the 909's TUNE, the sweep's time constant), through a diode limiter with
    // a DC bias, so it rounds into a sine and clips on its underside; a VCA with a 1.5 ms rise and
    // DECAY's fall; ATTACK (CLICK) a 1 ms pulse plus a noise burst through a band-pass around
    // 4.5 kHz (4 ms). An accent hits it all harder: louder, a higher start, more click. (Owen
    // Vallis's reading of the schematic: the triangle through a diode limiter, the phase reset, a DC
    // bias, three envelopes, the filtered attack noise.)
    float kick909 (Voice& v, float t)
    {
        const float f = v.f1 + (v.f0 - v.f1) * std::exp (-t / v.tau);
        if (v.t == 0.0) v.phase = 0.25;               // the reset: the triangle's zero, rising
        const float ph = (float) v.phase;
        const float tri = 4.0f * std::abs (ph - std::floor (ph + 0.5f)) - 1.0f;
        constexpr float kBias = -0.18f, kDrive = 1.8f;
        const float shaped = (fastTanh (kDrive * (tri + kBias)) - fastTanh (kDrive * kBias)) / fastTanh (kDrive);
        const float amp = (1.0f - std::exp (-t / 0.0015f)) * std::exp (-t / v.decay);
        float click = 0.0f;
        if (t < 0.03f && v.click > 0.0f)
        {
            const float pulse = t < 0.001f ? 1.0f : 0.0f;
            v.clickLp += 0.25f * (pulse - v.clickLp);   // the pulse, its edges rounded
            const float nz = v.clickBp.tickBP (rng.next()) * std::exp (-t / 0.004f);
            click = (0.55f * v.clickLp + 0.9f * nz) * v.click * (0.6f + 0.4f * v.accent);
        }
        v.phase += f / osr;
        if (v.phase > 1.0) v.phase -= 1.0;
        v.t += 1.0 / osr;
        if (t > v.decay * 9.0f + 0.05f) v.active = false;
        float y = (shaped * amp + click) * v.vel;
        if (v.drive > 0.0f) { const float g = 1.0f + v.drive * 6.0f; y = fastTanh (y * g) / std::sqrt (g) * 1.3f; }
        return dcBlock (v, y);
    }

    // TR-808 bass drum, after Werner, Abel and Smith (DAFx-14): a 1 ms trigger pulse (4-14 V with the
    // accent) through the pulse shaper (a low shelf: its rising edge whole, then settling to a
    // quarter; the falling edge swings below ground and a diode clamps it), into the bridged-T, a
    // band-pass ringing at about 49.5 Hz whose damping DECAY sets (the feedback buffer). For the first
    // ~6 ms the envelope generator lowers the bridged-T's resistance: the centre frequency jumps more
    // than an octave (PUNCH) and falls back as the envelope does (the trigger, then ~5 ms); then the
    // retriggering pulse kicks the network's centre once more, clamped to a diode drop, so the same
    // at any accent. Leakage through it makes the pitch sigh down over the first 300 ms (48-58 Hz in
    // their measurement: deeper with the accent, PITCH TIME its pace). TONE (CLICK) is the passive
    // low-pass after it; a high-pass takes the DC.
    float kick808 (Voice& v, float t)
    {
        constexpr float kPulse = 0.001f, kShelf = 0.25f, kShelfTau = 0.0004f, kClamp = 0.05f, kRetrig = 0.006f;
        const float a = 0.29f + 0.71f * v.accent;     // 4-14 V
        float e;
        if (t < kPulse) e = a * (kShelf + (1.0f - kShelf) * std::exp (-t / kShelfTau));
        else e = std::max (-kClamp, a * (1.0f - kShelf) * (std::exp (-t / kShelfTau) - std::exp (-(t - kPulse) / kShelfTau)));
        if (t >= kRetrig && t < kRetrig + 0.0005f) e += kClamp * 0.8f;   // the retriggering pulse
        const float env = t < kPulse ? 1.0f : std::exp (-(t - kPulse) / 0.0015f);
        const float hz = v.f1 * (1.0f + v.shift * env) * (1.0f + v.sigh * std::exp (-t / v.sighTau));
        if ((v.tick++ & 3) == 0)
        {
            const float q = kPi * hz * v.decay;        // the ring's time constant: DECAY
            v.bridgedT.set (hz, std::min (q, 400.0f), osr);
        }
        // Peak gain Q at resonance; a pulse of area A ms rings at about A * w0: scaled to about A.
        const float ring = v.bridgedT.tickBP (e) * (1.0f / (0.0005f * kTwoPi * v.f1));
        v.toneZ += v.toneK * (ring - v.toneZ);
        v.t += 1.0 / osr;
        if (t > v.decay * 9.0f + 0.05f) v.active = false;
        float y = v.toneZ * v.vel;
        if (v.drive > 0.0f) { const float g = 1.0f + v.drive * 6.0f; y = fastTanh (y * g) / std::sqrt (g) * 1.3f; }
        return dcBlock (v, y);
    }
    // 12 Hz high-pass (the output's coupling capacitor).
    float dcBlock (Voice& v, float x)
    {
        const float r = 1.0f - kTwoPi * 12.0f / (float) osr;
        v.dcY = x - v.dcX + r * v.dcY; v.dcX = x;
        return v.dcY;
    }

    // (SF-000) KICK FORGE: a hit's settings, from CHARACTER and the lane's knobs (trigger has set the pitch it lands on,
    // v.f1, and DECAY's and DENSITY's share of the decay is taken here).
    void forgeStart (Ctx& c, Voice& v, float vel, int gen, const SampleData* smp, bool main)
    {
        const float ch = c.v (FGCHAR);
        const ForgeShape f = forgeAt (ch);
        const float tone = c.v (FGTONE) * 2.0f - 1.0f;                           // -1 .. 1
        const float echo = std::pow (0.6f, (float) gen);
        v.f0 = v.f1 * (1.0f + (f.ratio - 1.0f) * std::pow (2.0f, (c.v (PUNCH) - 0.5f) * 2.0f));
        v.tau = f.tau * std::pow (2.0f, (c.v (PTIME) - 0.5f) * 3.0f);
        v.decay = f.decay * std::pow (2.0f, (c.v (DECAY) - 0.5f) * 4.0f) * echo;
        v.fm = f.fm * (1.0f + 0.5f * tone);
        v.fgHarm = std::max (0.0f, f.harm + 0.5f * tone * (0.25f + f.harm));
        v.drive = clamp01 (f.drive * 0.7f + c.v (DRIVE) * 0.8f);
        v.fgBody = 2.0f * c.v (FGBODY);
        v.fgSub = f.sub;
        v.fgLoK = 1.0f - std::exp (-kTwoPi * 120.0f / (float) osr);
        v.lp = std::min (18000.0f, f.lp * std::pow (4.0f, tone)) * echo;
        v.fgBright = std::min (6000.0f, v.lp * 2.0f);                           // (the filter opens on the hit and closes onto v.lp)
        v.click = c.v (CLICK) * f.impact * 1.2f;            // (the pulse on the front of it: as much as the impact, so next to none on the mellow kick)
        v.fgClip = f.clip * (1.0f + 1.5f * c.v (DRIVE));
        v.fgOut = forgeTrim (ch) / fastTanh (std::min (v.fgClip, 8.0f)) * 0.8f;
        // the impact: from the lane's file at POSITION (ninety thousandths of a second of it, a little either side from hit
        // to hit), brought to one strength; or noise
        v.fgImp = 2.0f * c.v (FGIMPACT) * f.impact * (0.9f + 0.2f * rng.uni());
        v.fgImpTau = f.impTau;
        v.fgHpK = 1.0f - std::exp (-kTwoPi * f.impHp / (float) osr);
        v.fgPos = -1.0;
        {
            // with no file POSITION is the noise's colour, a band from a low knock (250 Hz) to a bright tick (10 kHz), so
            // the knob is never one that does nothing
            const float hz = 250.0f * std::pow (40.0f, c.v (FGPOS));
            v.clickBp.set (hz, 1.6f, osr);
            v.fgNoise = 0.4f / (0.577f * std::sqrt (1.9635f * hz / (float) osr));
        }
        if (smp != nullptr && smp->length() > 4096 && v.fgImp > 0.0f)
        {
            const auto reg = regionFrom (c, SSTART, *smp, SGAIN);
            const double span = 0.09 * smp->sr, room = std::max (0.0, reg.end - reg.start - span - 8.0);
            double at = reg.start + (double) c.v (FGPOS) * room + (double) rng.next() * 0.0015 * smp->sr;
            at = std::min (std::max (at, reg.start), reg.start + room);
            // (its strength: the grain's own over what will sound of it, with its lows out as they will be when it plays)
            double sum = 0.0;
            const int n0 = (int) at, n1 = std::min (smp->length() - 2, n0 + (int) (0.03 * smp->sr));
            const float* l = smp->buf.getReadPointer (0); const float* r = smp->buf.getReadPointer (smp->buf.getNumChannels() > 1 ? 1 : 0);
            const float hk = 1.0f - std::exp (-kTwoPi * f.impHp / (float) smp->sr);
            float h1 = 0.0f, h2 = 0.0f;
            for (int n = n0; n < n1; ++n)
            {
                float x = 0.5f * (l[n] + r[n]);
                h1 += hk * (x - h1); x -= h1;
                h2 += hk * (x - h2); x -= h2;
                sum += (double) x * x;
            }
            const float rms = (float) std::sqrt (sum / (double) std::max (1, n1 - n0));
            v.fgImp *= std::min (300.0f, 0.4f / std::max (rms, 1.0e-5f));        // (a quiet place in a recording is brought up as far as 50 dB)
            v.fgPos = at;
            v.fgWide = smp->wide;             // (SF-000) a stereo file: the impact keeps its left and right
        }
        v.fgFeeds = main;
        if (main && gen == 0)
            ground.hit (f.groundT * std::pow (2.0f, (c.v (DECAY) - 0.5f) * 2.0f), 2.0f * c.v (FGGROUND) * f.ground * forgeTrim (ch), v.f1);
    }

    // One sample of it (at osr).
    float forgeVoice (Voice& v, float t, const SampleData* s, float& side)
    {
        // BODY: the wave starts at nought, rising, every hit
        const float f = v.f1 + (v.f0 - v.f1) * std::exp (-t / v.tau);
        const float w = kTwoPi * (float) v.phase;
        const float bend = v.fm * std::exp (-t / 0.03f);
        const float fund = std::sin (w + bend * v.fgPrev);
        v.fgPrev = fund;
        const float hE = v.fgHarm * std::exp (-t / (0.22f * v.decay + 0.012f));
        float body = fund;
        if (hE > 1.0e-4f) body += hE * (0.55f * std::sin (2.0f * w + 0.5f * bend * fund) + 0.30f * std::sin (3.0f * w));
        body *= std::min (1.0f, t / 0.0004f) * std::exp (-t / v.decay) * v.fgBody;
        if (v.fgFeeds) groundIn += body * v.fade * v.mix * v.vel;
        v.fgLo += v.fgLoK * (body - v.fgLo);
        float mid = body - v.fgLo;
        float sd = 0.0f;                    // (SF-000) a stereo file's impact: its left less its right, halved
        // IMPACT
        if (v.fgImp > 0.0f && t < 0.09f)
        {
            float x, xs = 0.0f;
            if (v.fgPos >= 0.0 && s != nullptr && (int) v.fgPos + 3 < s->length())
            {
                const float a = s->read (0, v.fgPos, 0.5), b = v.fgWide ? s->read (1, v.fgPos, 0.5) : a;
                x = 0.5f * (a + b);
                v.fgPos += 0.5;
                v.fgHp1 += v.fgHpK * (x - v.fgHp1); x -= v.fgHp1;       // (the file's lows are the body's to have)
                v.fgHp2 += v.fgHpK * (x - v.fgHp2); x -= v.fgHp2;
                if (v.fgWide)
                {
                    xs = 0.5f * (a - b);
                    v.fgHpS1 += v.fgHpK * (xs - v.fgHpS1); xs -= v.fgHpS1;
                    v.fgHpS2 += v.fgHpK * (xs - v.fgHpS2); xs -= v.fgHpS2;
                }
            }
            else x = v.clickBp.tickBP (rng.next()) * v.fgNoise;        // (the noise's band is its colour)
            const float tail = clamp01 ((t - 0.07f) / 0.02f);           // (and out by its end, whatever its decay)
            const float e = std::min (1.0f, t / 0.0003f) * std::exp (-t / v.fgImpTau) * (1.0f - tail * tail * (3.0f - 2.0f * tail));
            mid += x * e * v.fgImp;
            sd = xs * e * v.fgImp;
        }
        if (v.click > 0.0f && t < 0.0003f) mid += v.click * 0.6f;
        if (v.drive > 0.0f)
        {
            const float g = 1.0f + v.drive * 9.0f, u = mid * g, sat = fastTanh (u);
            mid = sat / std::sqrt (g) * 1.25f;
            if (sd != 0.0f) sd *= (std::abs (u) > 1.0e-4f ? sat / u : 1.0f) * g / std::sqrt (g) * 1.25f;
        }
        float y = (v.fgLo * v.fgSub + mid) * v.vel;
        sd *= v.vel;
        // the tone's low-pass: open on the hit, closing onto where TONE and CHARACTER have it
        const bool sideOn = v.fgWide && t < 0.2f;              // (its low-pass rings a little past the impact's end)
        if (v.lp < 15000.0f)
        {
            if ((v.tick++ & 15) == 0) { v.res.set (v.lp + v.fgBright * std::exp (-t / 0.03f), 0.7071f, osr); if (sideOn) { v.resS.g = v.res.g; v.resS.k = v.res.k; v.resS.a1 = v.res.a1; v.resS.a2 = v.res.a2; v.resS.a3 = v.res.a3; } }
            y = v.res.tickLP (y);
            if (sideOn) sd = v.resS.tickLP (sd);
        }
        {
            const float u = y * v.fgClip, sat = fastTanh (u);
            y = sat * v.fgOut;
            side = sideOn ? sd * (std::abs (u) > 1.0e-4f ? sat / u : 1.0f) * v.fgClip * v.fgOut : 0.0f;
        }
        v.phase += f / osr;
        if (v.phase > 1.0) v.phase -= 1.0;
        v.t += 1.0 / osr;
        if (t > v.decay * 9.0f + 0.06f) v.active = false;
        return y;
    }

    // One sample at the oversampled rate (osr).
    float renderVoice (Voice& v, const SampleData* s)
    {
        const float t = (float) v.t;
        float y = 0.0f;
        float side = 0.0f;                  // (SF-000) a stereo sample's left less its right, halved: y is their sum, halved, as it always was
        if (v.model == kKickForge) y = forgeVoice (v, t, s, side);
        else if (v.model == kKickSample)
        {
            if (s == nullptr || (int) v.phase + 2 >= (v.reg.end > 0.0 ? std::min (s->length(), v.reg.last()) : s->length())) { v.active = false; return 0.0f; }
            const double rate = v.srate * 0.5;               // input samples per output (2x) sample
            const float a = s->read (0, v.phase, rate), b = s->wide ? s->read (1, v.phase, rate) : a;
            y = 0.5f * (a + b) * v.vel;
            const float g = sampleGateGain (t, v.gate);
            if (g <= 0.0f) { v.active = false; return 0.0f; }
            y *= g;
            y *= v.reg.gain (v.phase);
            if (s->wide) side = 0.5f * (a - b) * v.vel * g * v.reg.gain (v.phase);
            v.t += 1.0 / osr;
            v.phase += v.srate * 0.5;
        }
        else if (v.model == 0) y = kick909 (v, t);
        else if (v.model == 1) y = kick808 (v, t);
        else
        {
            const float f = v.f1 + (v.f0 - v.f1) * std::exp (-t / v.tau);
            float ph = (float) v.phase;
            if (v.fm > 0.0f) ph += v.fm * rng.next() * 0.02f * std::exp (-t / 0.02f);
            const float sine = std::sin (kTwoPi * ph);
            const float tri = 4.0f * std::abs (ph - std::floor (ph + 0.5f)) - 1.0f;
            const float body = lerp (sine, -tri, v.shape * 0.6f);
            const float amp = std::min (1.0f, t / 0.0005f) * std::exp (-t / v.decay);
            float clickSig = 0.0f;
            if (v.click > 0.0f && t < 0.02f)
            {
                const float n = rng.next();
                const float a = std::exp (-kTwoPi * v.clickHz / (float) osr);
                v.lpZ = n + a * (v.lpZ - n);
                clickSig = (n - v.lpZ) * std::exp (-t / 0.004f) * v.click * 0.6f + (t < 0.0003f ? v.click * 0.8f : 0.0f);
            }
            y = (body * amp + clickSig) * v.vel;
            if (v.drive > 0.0f) { const float g = 1.0f + v.drive * 6.0f; y = fastTanh (y * g) / std::sqrt (g) * 1.3f; }
            if (v.lp < 15000.0f)
            {
                const float lpf = v.model == 4 ? v.lp + 4000.0f * std::exp (-t / 0.05f) : v.lp;
                if ((v.tick++ & 31) == 0) v.res.set (lpf, v.model == 4 ? 2.5f : 0.7071f, osr);
                y = v.res.tickLP (y);
            }
            v.phase += f / osr;
            if (v.phase > 1.0) v.phase -= 1.0;
            v.t += 1.0 / osr;
            if (t > v.decay * 9.0f + 0.05f) v.active = false;
        }
        if (v.fadeStep > 0.0f) { v.fade -= v.fadeStep; if (v.fade <= 0.0f) { v.active = false; return 0.0f; } y *= v.fade; side *= v.fade; }
        if (side != 0.0f) sideIn[v.layer == 1 ? 1 : 0] += side * v.cal * v.mix;
        return y * v.cal * v.mix;
    }

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midi) override
    {
        echoes.run (c, midi, events, c.v (DENS), echoSpaceBeats (c.ci (ESPACE)), c.v (ESWING), 1.0f, false);
        const SampleData* smp = sample.audio();
        const int model = c.ci (MODEL), layerModel = c.ci (LAYERMODEL);
        const float layer = c.v (LAYER);
        const float velSens = c.v (VELSENS);
        // LAYER SPLIT: SPLIT across the block; MODEL's gain against the layer as trigger gives it.
        const float k0 = c.v0 (SPLIT), k1 = c.v (SPLIT);
        split.set (c.v (SPLITHZ), 1.0f / (1.0f + layer), LayerSplit::amount (k1));
        const bool splitOff = split.off (LayerSplit::amount (k0), LayerSplit::amount (k1));
        size_t e = 0;
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        // (SF-000) a stereo sample (MODEL or the layer at SAMPLE, or KICK FORGE's impact from a file): its left less its
        // right goes beside the kick through halfbands and the split of its own, and is put back at the end. With none,
        // and none still ringing, the kick is one channel exactly as it was.
        auto reads = [] (int m) { return m == kKickSample || m == kKickForge; };
        const bool wide = (smp != nullptr && smp->wide && (reads (model) || (layer > 0.01f && reads (layerModel)))) || sideLive > 0;
        if (wide && ! wasWide) { decS.reset(); decSB.reset(); split.widen(); }
        wasWide = wide;
        for (int i = 0; i < c.n; ++i)
        {
            while (e < events.size() && events[e].offset <= i)
            {
                const auto& ev = events[e++];
                if (! ev.on) continue;
                const float vel = ev.gen == 0 ? lerp (1.0f, ev.vel, velSens) : ev.vel;
                // A hit cuts the ringing kick (3 ms fade), like retriggering a drum; a quiet DENSITY
                // ghost plays over it instead. Not inside trigger(): the layer must not fade the main voice.
                if (ev.gen == 0 || ev.gain >= 0.5f) fadeRinging();
                // Main and layer start together and are largely in phase: gains sum to 1, so the
                // layered kick never peaks above the single one.
                trigger (c, model, vel, ev.gen, ev.note, ev.gain / (1.0f + layer), true, i, smp, ev.gain);
                if (layer > 0.01f) trigger (c, layerModel, vel, ev.gen, ev.note, ev.gain * layer / (1.0f + layer), false, i, smp, ev.gain);
                lastHit = 1.0f;
            }
            // MODEL's voices and the layer's sum apart, each through its own halfband, then split
            // (or added: with no layer, exactly the sound there was before the split).
            float y0 = 0.0f, y1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
            groundIn = 0.0f; sideIn[0] = sideIn[1] = 0.0f;
            for (auto& v : voices) if (v.active) (v.layer == 1 ? b0 : y0) += renderVoice (v, smp);
            if (ground.awake()) y0 += ground.tick (groundIn);              // (SF-000) KICK FORGE's ground, with MODEL's voices
            const float sa0 = sideIn[0], sb0 = sideIn[1];
            groundIn = 0.0f; sideIn[0] = sideIn[1] = 0.0f;
            for (auto& v : voices) if (v.active) (v.layer == 1 ? b1 : y1) += renderVoice (v, smp);
            if (ground.awake()) y1 += ground.tick (groundIn);
            const float ya = dec.down (y0, y1), yb = decB.down (b0, b1);
            if (wide)
            {
                if (sa0 != 0.0f || sb0 != 0.0f || sideIn[0] != 0.0f || sideIn[1] != 0.0f) sideLive = 4096;      // (and the filters' tails after)
                else if (sideLive > 0) --sideLive;
                const float sa = decS.down (sa0, sideIn[0]), sb = decSB.down (sb0, sideIn[1]);
                float l = ya + sa, r = ya - sa;
                if (splitOff) { l += yb + sb; r += yb - sb; }
                else split.tick (l, r, yb + sb, yb - sb, LayerSplit::amount (k0 + (k1 - k0) * (float) i / (float) c.n));
                L[i] = l; R[i] = r;
                hist.push (std::fabs (0.5f * (l + r)));
                continue;
            }
            const float y = splitOff ? ya + yb : split.tick (ya, yb, LayerSplit::amount (k0 + (k1 - k0) * (float) i / (float) c.n));
            L[i] = y; R[i] = y;
            hist.push (std::fabs (y));
        }
        out.process (c, io, FILT, RES, 0.0f, 0.0f, c.b (ALLOCON), LEVEL);
    }
    // OUTPUT; under it, with a sample loaded, the sample (its START and END to drag).
    int displayHeight() const override { return kOwn + (sample.get() != nullptr ? SampleStrip::kHeight : 0); }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto s = sample.get();
        const auto st = SampleStrip::frameIn (r, kOwn);
        if (s != nullptr && ! st.isEmpty()) strip.paint (g, st, *s, *this, SSTART, SCUTS);
        r = r.withHeight ((float) kOwn);
        ui::frame (g, r, "OUTPUT");
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
        if (s == nullptr) ui::label (g, "NO SAMPLE", r.reduced (8, 2).removeFromBottom (14), ui::kWords, 10.0f, juce::Justification::centredRight);
        else if (st.isEmpty()) ui::label (g, "SAMPLE " + s->name.toUpperCase(), r.reduced (8, 2).removeFromBottom (14), ui::kWords, 10.0f, juce::Justification::centredRight);
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        if (auto smp = sample.get(); smp != nullptr) { if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty()) { strip.mouseKit (p, SampleStrip::wave (st), down, *this, SSTART, SCUTS, *smp); return; } }
        if (down) strip.up();
    }
    void displayMouseUp() override { strip.up(); }
    void displayDouble (juce::Point<float> p, juce::Rectangle<float> r) override
    {
        if (auto smp = sample.get(); smp != nullptr) if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty()) SampleStrip::doubleKit (p, SampleStrip::wave (st), *this, SSTART, SCUTS, *smp);
    }
private:
    static constexpr int kOwn = 70;     // OUTPUT
    SampleStrip strip;
    struct DecimHist { std::atomic<float> v[512] {}; int w = 0, k = 0; float acc = 0; void push (float x) { acc = std::max (acc, x); if (++k >= 512) { v[w].store (std::min (1.0f, acc)); w = (w + 1) % 512; k = 0; acc = 0; } } };
    double sr = 48000.0, osr = 96000.0;
    Halfband dec, decB;                 // MODEL's voices, the layer's
    Halfband decS, decSB;               // (SF-000) a stereo sample's left less its right: MODEL's, the layer's
    float sideIn[2] = { 0.0f, 0.0f };   // (what the voices gave of it this sample)
    int sideLive = 0; bool wasWide = false;
    LayerSplit split;
    Ground ground; float groundIn = 0.0f;   // (SF-000) KICK FORGE's ground, and what the body gives it this sample
    static constexpr int kVoices = 8;
    std::array<Voice, kVoices> voices;
    EchoScheduler echoes;
    std::vector<NoteEvent> events;
    RoleOutput out;
    SampleSlot sample;
    Noise rng;
    int sliceHits = 0; Noise sliceRng;      // (SF-000) slices: hits counted for CYCLE, and its own chance (the drum's is not disturbed)
    float lastHit = 0;
    DecimHist hist;
};
ModulePtr makeKICK() { return std::make_unique<KickModule>(); }

// =====================================================================================  PERC
// [Nov 10] The 808's hat and the 909's hats and cymbals as their circuits (P_HAT808, P_ROM909), and
// the 808's and 909's snares and toms, appended after SAMPLE (a model's number never moves).
enum PercType { P_METAL, P_CLAP, P_RIM, P_BELL, P_CLAVE, P_SHAKER, P_FM, P_MODAL, P_SAMPLE,
                P_HAT808, P_ROM909, P_SN808, P_SN909, P_TOM808, P_TOM909 };
struct PercModel { const char* name; PercType type; float freqMul, bpHz, hpHz, noise, decay; };
// P_ROM909: noise is the sample's own decay in the ROM (seconds). P_SN*/P_TOM*: freqMul the body's pitch (Hz).
static const PercModel kPercModels[] = {
    { "HAT 909-TYPE",  P_ROM909, 1.30f,  9000.0f, 7000.0f, 0.12f, 0.09f },
    { "HAT 808-TYPE",  P_HAT808, 1.00f,  7100.0f, 7500.0f, 0.00f, 0.045f },
    { "HAT 606-TYPE",  P_METAL,  1.60f, 11000.0f, 9000.0f, 0.60f, 0.05f },
    { "RIDE 909-TYPE", P_ROM909, 1.10f,  6000.0f, 3000.0f, 1.40f, 1.20f },
    { "CRASH",         P_ROM909, 1.20f,  7000.0f, 4000.0f, 1.90f, 1.80f },
    { "CLAP",          P_CLAP,   1.00f,  1200.0f,  400.0f, 1.00f, 0.15f },
    { "RIM",           P_RIM,    1.00f,  1700.0f,  400.0f, 0.00f, 0.02f },
    { "COWBELL",       P_BELL,   1.00f,  1000.0f,  300.0f, 0.00f, 0.25f },
    { "CLAVE",         P_CLAVE,  1.00f,  2500.0f,  200.0f, 0.00f, 0.04f },
    { "SHAKER",        P_SHAKER, 1.00f,  6000.0f, 2000.0f, 1.00f, 0.08f },
    { "METAL FM",      P_FM,     1.00f,   400.0f,  200.0f, 0.00f, 0.30f },
    { "MODAL",         P_MODAL,  1.00f,   300.0f,   60.0f, 0.00f, 0.60f },
    { "SAMPLE",        P_SAMPLE, 1.00f,  1000.0f,   20.0f, 0.00f, 1.00f },
    { "SNARE 808-TYPE", P_SN808, 238.0f, 0.0f, 1500.0f, 0.00f, 0.12f },
    { "SNARE 909-TYPE", P_SN909, 185.0f, 0.0f, 1000.0f, 0.00f, 0.10f },
    { "TOM 808-TYPE",  P_TOM808, 100.0f, 0.0f,  200.0f, 0.00f, 0.30f },
    { "TOM 909-TYPE",  P_TOM909, 110.0f, 0.0f,  300.0f, 0.00f, 0.30f } };
static constexpr int kNumPercModels = 17;
static std::vector<std::string> percModelNames() { std::vector<std::string> v; for (auto& m : kPercModels) v.push_back (m.name); return v; }

class PercModule : public Module
{
public:
    // SSTART: the sample's START; END, FADE IN, FADE OUT follow (sampleRegionParams).
    enum { TUNE, DECAY, TONE, DENS, FILT, ACCENT, DRIVE, LEVEL, MODEL, ESPACE, ESWING, EPROB, CHOKE, SPREAD, RES, ALLOCON, SSTART, SGAIN = SSTART + 4, SNORM, SSLICE, SCYCLE, SBARS, SCUTS };     // (SF-000) GAIN, NORMALISE, SLICE, CYCLE, LOOP, MARKERS (and the 32 after it)
    std::string id() const override { return "PERC"; }
    bool instrument() const override { return true; }
    std::vector<ParamSpec> params() const override
    {
        return withCuts (withLoop (withSlices (withSampleGain (withRegion ({ knob ("tune", "TUNE", -24.0f, 24.0f, 0.0f, "st").prim(),
                 pct ("decay", "DECAY", 0.5f).prim().dir (1),
                 pct ("tone", "TONE", 0.5f).prim(),
                 pct ("density", "DENSITY", 0.0f).prim().dir (1),
                 bip ("filter", "FILTER").prim(),
                 pct ("accent", "ACCENT", 0.5f).prim(),
                 pct ("drive", "DRIVE", 0.0f).prim(),
                 levelParam (-6.0f).prim(),
                 choice ("model", "MODEL", percModelNames(), 0),
                 choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 1),
                 pct ("echo_swing", "ECHO SWING", 0.0f).slow(),
                 pct ("echo_prob", "ECHO PROB", 1.0f).slow(),
                 toggle ("choke", "CHOKE", true),
                 pct ("spread", "SPREAD", 0.3f).slow(),
                 pct ("res", "FILTER RES", 0.1f),
                 toggle ("alloc", "ALLOC", true).noPreset().forget() })))));
    }
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP ONE-SHOT (MODEL: SAMPLE)"; }
    void loadFiles (const juce::StringArray& f) override { if (f.size() > 0) sample.set (loadSampleFile (juce::File (f[0]), sr)); }
    void save (juce::ValueTree& t) const override { sample.save (t, "SAMPLE"); }
    void load (const juce::ValueTree& t) override { sample.load (t, "SAMPLE", sr); }
    void prepare (double rate, int mb) override
    {
        if (auto s = sample.get(); s != nullptr && std::abs (s->sr - rate) > 1 && s->flac.getSize() > 0) sample.set (loadSampleFlac (s->flac, s->name, rate, s->flacGain));
        sr = rate; osr = 2.0 * rate;
        waves = WaveSet::get (osr);
        for (auto& d : dec) d.set (HalfbandDesign::forStage (rate, 0));
        out.prepare (rate, mb, ROLE_PERC, "PERC", false);
        events.reserve (2048);
        reset();
    }
    void reset() override
    {
        echoes.reset();
        lp.reset();                                 // (SF-000)
        for (auto& v : voices) v.active = false;
        for (auto& d : dec) d.reset();
        out.reset();
    }

    struct Voice
    {
        bool active = false;
        PercType type = P_METAL;
        double t = 0;
        float vel = 1, decay = 0.1f, pan = 0.5f, gl = 0.7071f, gr = 0.7071f, fade = 1.0f, fadeStep = 0.0f, tune = 1.0f, tone = 0.5f, noise = 0, cal = 1.0f;
        double ph[6] {};
        float freqs[6] {};
        Svf bp, hp, res[6];
        int oct[6] {};
        double spos = 0;
        float gate = 1.0e9f;
        SampleRegion reg;                   // SAMPLE: the part of the sample this hit plays
        float tilt = 0.0f, tiltLp = 0.0f, tiltA = 0.0f, tiltLo = 1.0f, tiltHi = 1.0f;      // (SF-000) TONE on a sample
        float side = 0.0f, tiltLp2 = 0.0f;  // (SF-000) a sample's left less its right, halved (what the hit returns is their sum, halved), and TONE's state for it
        // The circuits: the 909's ROM (its sample's own decay, the held 6-bit value), filters, the
        // body's pitch, the snappy noise's level and decay, a DC blocker.
        float romDecay = 0.1f, romHeld = 0.0f, body = 200.0f, snappy = 0.5f, nzDecay = 0.0f, dcX = 0.0f, dcY = 0.0f, lpZ = 0.0f;
        int romCount = 0, romStep = 1;
        Svf lp, hp2;
        float level() const { return active ? vel * std::exp (-(float) t / std::max (0.005f, decay)) : 0.0f; }
    };

    void trigger (Ctx& c, int model, const NoteEvent& ev)
    {
        // CHOKE: a hit cuts the previous one; a quiet DENSITY ghost doesn't.
        if (c.b (CHOKE) && (ev.gen == 0 || ev.gain >= 0.5f))
            for (auto& v : voices) if (v.active && v.fadeStep == 0.0f) v.fadeStep = 1.0f / (0.004f * (float) osr);
        int idx = pickVoice (voices);
        auto& v = voices[(size_t) idx];
        const auto& m = kPercModels[model];
        v = Voice();
        v.active = true; v.type = m.type;
        const float acc = c.v (ACCENT);
        v.vel = lerp (0.7f, ev.vel, acc) * ev.gain;         // ACCENT shapes the velocity; the DENSITY fade comes after it
        v.tune = semis (c.v (TUNE) + c.bend);       // (SF-000: and the pitch wheel, as each hit starts)
        v.tone = c.v (TONE);
        // (SF-000) TONE on a sample: a tilt about 2 kHz, darker under the centre, brighter over it, none at it
        v.tilt = (v.tone - 0.5f) * 2.0f; v.tiltLp = 0.0f; v.tiltLp2 = 0.0f; v.side = 0.0f;
        v.tiltA = 1.0f - std::exp (-kTwoPi * 2000.0f / (float) osr); v.tiltLo = std::pow (10.0f, -v.tilt * 0.6f); v.tiltHi = std::pow (10.0f, v.tilt * 0.6f);
        v.decay = m.decay * std::pow (2.0f, (c.v (DECAY) - 0.5f) * 4.0f) * std::pow (0.65f, (float) ev.gen);
        v.noise = m.noise;
        v.pan = 0.5f + (ev.gen == 0 ? 0.0f : ((ev.gen & 1) ? -1.0f : 1.0f) * c.v (SPREAD) * 0.5f * (rng.uni() * 0.5f + 0.5f));
        v.cal = cal::at (cal::perc, model);
        v.gl = std::sqrt (1.0f - v.pan) * v.cal; v.gr = std::sqrt (v.pan) * v.cal;
        static const float metal[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        static const float modal[6] = { 1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f };
        const float dark = std::pow (0.7f, (float) ev.gen);
        switch (m.type)
        {
            case P_METAL:
                for (int k = 0; k < 6; ++k) v.freqs[k] = metal[k] * m.freqMul * v.tune * (0.8f + v.tone * 0.4f);
                v.bp.set (m.bpHz * v.tune * (0.6f + v.tone * 0.8f) * dark, 1.2f, osr);
                v.hp.set (m.hpHz * (0.6f + v.tone * 0.8f) * dark, 0.7071f, osr);
                break;
            case P_CLAP: v.bp.set (m.bpHz * v.tune * (0.7f + v.tone * 0.6f) * dark, 1.5f, osr); v.hp.set (m.hpHz, 0.7071f, osr); break;
            case P_RIM: v.freqs[0] = 1700.0f * v.tune * (0.7f + v.tone * 0.6f); v.freqs[1] = 500.0f * v.tune; v.hp.set (400.0f * (0.5f + v.tone), 0.7071f, osr); break;      // (SF-000) TONE: its click's pitch and how thin it is (as it was, at the centre)
            case P_BELL: v.freqs[0] = 540.0f * v.tune; v.freqs[1] = 800.0f * v.tune; v.bp.set (1000.0f * v.tune * (0.6f + v.tone * 0.8f), 1.0f, osr); break;
            case P_CLAVE: v.freqs[0] = 2500.0f * v.tune * (0.7f + v.tone * 0.6f); break;
            case P_SHAKER: v.bp.set (std::min (6000.0f * (0.5f + v.tone) * dark * v.tune, 0.45f * (float) osr), 0.8f, osr); v.hp.set (std::min (2000.0f * v.tune, 0.4f * (float) osr), 0.7071f, osr); break;      // (SF-000) TUNE moves its band
            case P_FM: v.freqs[0] = 400.0f * v.tune; v.freqs[1] = v.freqs[0] * (1.2f + v.tone * 1.2f); break;
            case P_MODAL:
                for (int k = 0; k < 6; ++k)
                {
                    v.freqs[k] = 300.0f * v.tune * std::pow (modal[k], 0.7f + v.tone * 0.6f);
                    v.res[k].set (std::min (v.freqs[k], 0.45f * (float) osr), 60.0f + v.decay * 400.0f, osr);
                }
                break;
            case P_HAT808:
                // The six oscillators the 808's cymbal, hats and cowbell share (205.3, 304.4, 369.6,
                // 522.7, ~540, ~800 Hz, duty 47.98 %), the hats' band-pass at 7.1 kHz, then two
                // high-passes resonant towards 10 kHz (Werner, Abel, Smith: the TR-808 cymbal).
                for (int k = 0; k < 6; ++k) v.freqs[k] = metal[k] * v.tune;
                v.bp.set (m.bpHz * (0.75f + v.tone * 0.5f) * dark, 1.6f, osr);
                v.hp.set (m.hpHz * (0.8f + v.tone * 0.4f) * dark, 0.7071f, osr);
                v.hp2.set (std::min (10500.0f * dark, 0.4f * (float) osr), 1.6f, osr);
                break;
            case P_ROM909:
                // The "sample": a metal and noise mix with its own decay, as the ROM held a recorded cymbal.
                // Made at the ROM's rate (about 32 kHz: one value held over romStep samples here).
                for (int k = 0; k < 6; ++k) v.freqs[k] = metal[k] * m.freqMul * 2.1f * v.tune;
                v.romDecay = m.noise * (0.7f + 0.6f * v.tone);
                v.romStep = std::max (1, (int) std::lround (osr / 32000.0));
                v.bp.set (std::min (m.bpHz * v.tune * (0.7f + v.tone * 0.6f) * dark, 14000.0f), 0.9f, osr / v.romStep);
                v.hp.set (m.hpHz * dark, 0.7071f, osr / v.romStep);
                v.lp.set (std::min (11500.0f, 0.42f * (float) osr), 0.7071f, osr);   // the DAC's reconstruction filter
                break;
            case P_SN808: case P_SN909: case P_TOM808: case P_TOM909:
                v.body = m.freqMul * v.tune;
                v.snappy = v.tone;
                v.nzDecay = (m.type == P_SN909 ? 0.08f + 0.22f * v.tone : 0.06f) * std::pow (2.0f, (c.v (DECAY) - 0.5f) * 2.0f) * std::pow (0.65f, (float) ev.gen);
                // Bridged-T rings: Q = pi f tau (the upper one dies twice as fast).
                v.res[0].set (v.body, std::min (kPi * v.body * v.decay, 400.0f), osr);
                v.res[1].set (v.body * 2.0f, std::min (kPi * v.body * 2.0f * v.decay * 0.5f, 400.0f), osr);
                v.hp.set (m.hpHz * dark, 0.7071f, osr);
                v.lp.set (std::min (7000.0f * dark, 0.42f * (float) osr), 0.7071f, osr);
                v.bp.set (2500.0f * dark, 0.8f, osr);
                break;
            case P_SAMPLE:
                // From START; DECAY's gate measures the part that plays.
                if (auto* s = sample.audio())
                {
                    const Cuts cuts = cutsFrom (c, SCUTS, *s);
                    v.reg = hitRegion (c, *s, regionFrom (c, SSTART, *s, SGAIN), SSLICE, sliceHits++, sliceRng.uni(), &cuts);       // (SF-000) the whole of it, or a slice
                    v.spos = v.reg.start;
                    v.gate = sampleGateSeconds (c.v (DECAY), (float) (v.reg.length() / s->sr) / v.tune, ev.gen);
                }
                break;
        }
        for (int k = 0; k < 6; ++k) v.oct[k] = waves->square.octaveFor (v.freqs[k]);
    }

    // One sample at the oversampled rate (osr); squares are band-limited (mipmapped tables).
    float render (Voice& v, const SampleData* s)
    {
        const auto& W = *waves;
        const float t = (float) v.t;
        float y = 0.0f;
        switch (v.type)
        {
            case P_METAL:
            {
                float sq = 0.0f;
                for (int k = 0; k < 6; ++k) { sq += W.square.read (v.oct[k], v.ph[k]); v.ph[k] += v.freqs[k] / osr; wrap01 (v.ph[k]); }
                const float src = sq / 6.0f * (1.0f - v.noise) + rng.next() * v.noise;
                y = v.hp.tickHP (v.bp.tickBP (src) * 2.0f) * std::exp (-t / v.decay) * std::min (1.0f, t / 0.0003f);
                break;
            }
            case P_CLAP:
            {
                float env = 0.0f;
                for (int b = 0; b < 3; ++b) { const float tb = t - b * 0.011f; if (tb >= 0) env = std::max (env, std::exp (-tb / 0.007f)); }
                env = std::max (env, t > 0.022f ? 0.6f * std::exp (-(t - 0.022f) / v.decay) : 0.0f);
                y = v.hp.tickHP (v.bp.tickBP (rng.next()) * 3.0f) * env;
                break;
            }
            case P_RIM:
                v.ph[0] += v.freqs[0] / osr; v.ph[1] += v.freqs[1] / osr;
                y = v.hp.tickHP (std::sin (kTwoPi * (float) v.ph[0]) * 0.6f + std::sin (kTwoPi * (float) v.ph[1]) * 0.5f) * std::exp (-t / v.decay);
                break;
            case P_BELL:
            {
                const float sq = W.square.read (v.oct[0], v.ph[0]) + W.square.read (v.oct[1], v.ph[1]);
                for (int k = 0; k < 2; ++k) { v.ph[k] += v.freqs[k] / osr; wrap01 (v.ph[k]); }
                const float env = 0.6f * std::exp (-t / 0.012f) + 0.4f * std::exp (-t / v.decay);
                y = v.bp.tickBP (sq) * env * 0.6f;
                break;
            }
            case P_CLAVE: v.ph[0] += v.freqs[0] / osr; y = std::sin (kTwoPi * (float) v.ph[0]) * std::exp (-t / v.decay); break;
            case P_SHAKER: y = v.hp.tickHP (v.bp.tickBP (rng.next()) * 2.0f) * std::min (1.0f, t / 0.008f) * std::exp (-t / v.decay); break;
            case P_FM:
            {
                const float idx = 6.0f * std::exp (-t / (v.decay * 0.5f));
                v.ph[1] += v.freqs[1] / osr; if (v.ph[1] > 1.0) v.ph[1] -= 1.0;
                v.ph[0] += v.freqs[0] / osr; if (v.ph[0] > 1.0) v.ph[0] -= 1.0;
                y = std::sin (kTwoPi * (float) v.ph[0] + idx * std::sin (kTwoPi * (float) v.ph[1])) * std::exp (-t / v.decay);
                break;
            }
            case P_MODAL:
            {
                const float ex = t < 0.002f ? rng.next() * (1.0f - t / 0.002f) : 0.0f;
                for (int k = 0; k < 6; ++k) y += v.res[k].tickBP (ex) / (1.0f + k * 0.4f);
                y *= 0.6f;
                break;
            }
            case P_HAT808:
            {
                // Six band-limited pulses at 47.98 % (two saws), the band-pass, then the swing-type VCA:
                // the envelope is the ceiling the signal clips against (sharpness 3.5), so the hat is
                // gritty and compressed while it rings and shuts off as the envelope falls.
                float sq = 0.0f;
                for (int k = 0; k < 6; ++k)
                {
                    sq += W.saw.read (v.oct[k], v.ph[k]) - W.saw.read (v.oct[k], v.ph[k] + 0.5202);
                    v.ph[k] += v.freqs[k] / osr; wrap01 (v.ph[k]);
                }
                const float band = v.bp.tickBP (sq / 6.0f) * 3.0f;
                const float env = std::min (1.0f, t / 0.0001f) * std::exp (-t / v.decay);
                const float swing = env > 1.0e-4f ? env * fastTanh (3.5f * band / env) / 3.5f * 2.0f : 0.0f;
                y = v.hp2.tickHP (v.hp.tickHP (swing)) * 1.6f;
                break;
            }
            case P_ROM909:
            {
                // The 909 played its cymbals from 6-bit ROM: the recorded sound (with its own decay) is
                // quantised to 64 steps at about 32 kHz and held, through the DAC's reconstruction
                // low-pass; the analog VCA (DECAY) shapes it after. So the tail is coarse and grainy
                // where the sample was quiet, whatever DECAY does.
                if (--v.romCount <= 0)
                {
                    v.romCount = v.romStep;
                    float sq = 0.0f;
                    for (int k = 0; k < 6; ++k) { sq += W.square.read (v.oct[k], v.ph[k]); }
                    const float src = v.hp.tickHP (v.bp.tickBP (0.55f * sq / 6.0f + 0.45f * rng.next()) * 2.2f);
                    const float rom = src * std::min (1.0f, t / 0.0002f) * std::exp (-t / v.romDecay);
                    v.romHeld = std::round (juce::jlimit (-1.0f, 1.0f, rom) * 31.0f) / 31.0f;
                }
                for (int k = 0; k < 6; ++k) { v.ph[k] += v.freqs[k] / osr; wrap01 (v.ph[k]); }
                y = v.lp.tickLP (v.romHeld) * std::exp (-t / v.decay);
                break;
            }
            case P_SN808:
            {
                // Two bridged-T resonators (238 and 476 Hz, rung by the 1 ms trigger pulse, the upper one
                // dying faster) and the snappy: noise through a high-pass, its own envelope (TONE: how much).
                const float pulse = t < 0.001f ? 1.0f : 0.0f;
                const float a = v.res[0].tickBP (pulse) * (1.0f / (0.001f * kTwoPi * v.body));
                const float b = v.res[1].tickBP (pulse) * (1.0f / (0.002f * kTwoPi * v.body));
                const float nz = v.hp.tickHP (rng.next()) * std::exp (-t / v.nzDecay) * std::min (1.0f, t / 0.0005f);
                y = (a + 0.6f * b) * 0.8f + nz * v.snappy * 0.9f;
                break;
            }
            case P_SN909:
            {
                // Two triangle VCOs (185 and 330 Hz) swept down a little in the first 8 ms, through the
                // 909's diode limiter; the snappy: noise through a low-pass and a high-pass, TONE its
                // level and length (the 909's SNAPPY and TONE).
                const float sweep = 1.0f + 0.6f * std::exp (-t / 0.008f);
                float body = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    const float ph = (float) v.ph[k];
                    const float tri = 4.0f * std::abs (ph - std::floor (ph + 0.5f)) - 1.0f;
                    body += (fastTanh (1.8f * (tri - 0.1f)) - fastTanh (-0.18f)) / fastTanh (1.8f) * (k == 0 ? 1.0f : 0.55f);
                    v.ph[k] += v.body * (k == 0 ? 1.0f : 1.78f) * sweep / osr; wrap01 (v.ph[k]);
                }
                const float nz = v.hp.tickHP (v.lp.tickLP (rng.next())) * std::exp (-t / v.nzDecay);
                y = body * std::exp (-t / v.decay) * 0.6f + nz * (0.2f + 0.8f * v.snappy);
                break;
            }
            case P_TOM808:
            {
                // A bridged-T as the kick's, higher: its pitch jumps for the trigger and a few ms after,
                // then settles; TONE a soft noise burst through a band-pass.
                const float pulse = t < 0.001f ? 1.0f : 0.0f;
                const float hz = v.body * (1.0f + 0.5f * (t < 0.001f ? 1.0f : std::exp (-(t - 0.001f) / 0.002f)));
                if ((v.romCount++ & 3) == 0) v.res[0].set (hz, std::min (kPi * hz * v.decay, 400.0f), osr);
                const float ring = v.res[0].tickBP (pulse) * (1.0f / (0.0005f * kTwoPi * v.body));
                const float nz = v.bp.tickBP (rng.next()) * std::exp (-t / 0.02f) * v.snappy * 0.4f;
                y = ring * 0.13f + nz * 0.5f;       // (SF-000) TONE's burst was 0.2 of this: too faint to hear the knob
                break;
            }
            case P_TOM909:
            {
                // A triangle VCO through the diode limiter, swept down from 1.8 times its pitch (30 ms),
                // the VCA's decay; TONE a noise burst through a band-pass.
                const float hz = v.body * (1.0f + 0.8f * std::exp (-t / 0.03f));
                const float ph = (float) v.ph[0];
                const float tri = 4.0f * std::abs (ph - std::floor (ph + 0.5f)) - 1.0f;
                const float shaped = (fastTanh (1.8f * (tri - 0.15f)) - fastTanh (-0.27f)) / fastTanh (1.8f);
                v.ph[0] += hz / osr; wrap01 (v.ph[0]);
                const float nz = v.bp.tickBP (rng.next()) * std::exp (-t / 0.015f) * v.snappy * 0.5f;
                y = (shaped * std::min (1.0f, t / 0.001f) * std::exp (-t / v.decay) * 0.8f + nz) * 0.45f;
                break;
            }
            case P_SAMPLE:
            {
                v.side = 0.0f;
                if (s == nullptr || (int) v.spos + 2 >= (v.reg.end > 0.0 ? std::min (s->length(), v.reg.last()) : s->length())) { v.active = false; return 0.0f; }
                const float g = sampleGateGain (t, v.gate);
                if (g <= 0.0f) { v.active = false; return 0.0f; }
                // (SF-000) in stereo: the two channels' sum is the hit, as it always was, and their difference goes with it
                // (v.side), to be put back as left and right where the voices are mixed
                const float a = s->read (0, v.spos, v.tune * 0.5), b = s->wide ? s->read (1, v.spos, v.tune * 0.5) : a;
                const float rg = v.reg.gain (v.spos);
                y = 0.5f * (a + b) * g * rg;
                float sd = s->wide ? 0.5f * (a - b) * g * rg : 0.0f;
                v.spos += v.tune * 0.5;
                if (std::abs (v.tilt) > 0.002f)       // (SF-000) TONE
                {
                    v.tiltLp += v.tiltA * (y - v.tiltLp); y = v.tiltLp * v.tiltLo + (y - v.tiltLp) * v.tiltHi;
                    v.tiltLp2 += v.tiltA * (sd - v.tiltLp2); sd = v.tiltLp2 * v.tiltLo + (sd - v.tiltLp2) * v.tiltHi;
                }
                v.side = sd;
                break;
            }
        }
        if (v.type >= P_SN808)                // the bodies' DC (the biased limiters): a 12 Hz high-pass
        {
            const float rr = 1.0f - kTwoPi * 12.0f / (float) osr;
            v.dcY = y - v.dcX + rr * v.dcY; v.dcX = y; y = v.dcY;
        }
        v.t += 1.0 / osr;
        if (v.type != P_SAMPLE && t > std::max (v.decay, v.nzDecay) * 8.0f + 0.03f) v.active = false;
        if (v.fadeStep > 0.0f) { v.fade -= v.fadeStep; if (v.fade <= 0.0f) { v.active = false; v.side = 0.0f; return 0.0f; } y *= v.fade; v.side *= v.fade; }
        v.side *= v.vel;
        return y * v.vel;
    }

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midi) override
    {
        echoes.run (c, midi, events, c.v (DENS), echoSpaceBeats (c.ci (ESPACE)), c.v (ESWING), c.v (EPROB), false);
        const SampleData* smp = sample.audio();
        const int model = c.ci (MODEL);
        const float drv = c.v (DRIVE);
        const float dg = 1.0f + drv * 8.0f, dn = 1.0f / (1.0f + drv * 2.0f);
        // (SF-000) LOOP: the sample goes round on the rig's bars (see loopSetup). The lane's hits then open it and do not
        // start it again.
        const bool loop = loopSetup (c, model, smp);
        size_t e = 0;
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        for (int i = 0; i < c.n; ++i)
        {
            while (e < events.size() && events[e].offset <= i)
            {
                const auto& ev = events[e++];
                if (! ev.on) continue;
                if (loop) loopHit (c, ev); else trigger (c, model, ev);
            }
            float l[2] {}, r[2] {};
            for (int k = 0; k < 2; ++k)
            {
                if (lp.live()) loopTick (smp, drv, dg, dn, l[k], r[k]);
                for (auto& v : voices)
                    if (v.active)
                    {
                        float y = render (v, smp);
                        if (v.side != 0.0f)             // (SF-000) a stereo sample: its own left and right, each through DRIVE, then where the hit sits
                        {
                            float a = y + v.side, b = y - v.side;
                            if (drv > 0.001f) { a = fastTanh (a * dg) * dn; b = fastTanh (b * dg) * dn; }
                            l[k] += a * v.gl; r[k] += b * v.gr;
                            continue;
                        }
                        if (drv > 0.001f) y = fastTanh (y * dg) * dn;
                        l[k] += y * v.gl; r[k] += y * v.gr;
                    }
            }
            L[i] = dec[0].down (l[0], l[1]) * 1.41f;
            R[i] = dec[1].down (r[0], r[1]) * 1.41f;
        }
        out.process (c, io, FILT, RES, 0.0f, 0.0f, c.b (ALLOCON), LEVEL);
    }
    // (SF-000) LOOP. A sample on a hat or the clap is a hit: every step starts it again, and DECAY cuts it. With LOOP on
    // (SAMPLE the model, a file loaded) it is a loop instead: START..END is taken to be a number of bars (AUTO: the power
    // of two it is nearest to at the tempo; or the count chosen) and goes round on the rig's bars while the clock runs,
    // at the rig's tempo, so its pitch moves with the tempo's ratio to its own, as a record's does. Where it is in the
    // file is where the clock is in those bars, whenever it is heard: stopping, starting or opening it late never puts
    // it out. TUNE turns it by whole octaves only (twice as fast, half as fast: still on the bars). DECAY right up (90 %
    // and over): it plays through, at one level. Under that the lane's steps open it, each for as long as DECAY says
    // (10 ms to a bar) and as loud as the hit is (ACCENT, DENSITY and the echoes as on any hit): the loop, chopped by the
    // pattern. In stereo, if the file is; TONE, DRIVE and the rest of the lane after it as on a hit. Stopped: silence.
    SampleLoop lp;
    bool loopSetup (Ctx& c, int model, const SampleData* smp)
    {
        return lp.setup (c, c.ci (SBARS), kPercModels[model].type == P_SAMPLE, smp, smp != nullptr ? regionFrom (c, SSTART, *smp, SGAIN) : SampleRegion(),
                         osr, cal::at (cal::perc, model), c.v (TONE), c.v (DECAY), c.v (TUNE));
    }
    void loopHit (Ctx& c, const NoteEvent& ev) { lp.hit (c, c.v (DECAY), lerp (0.7f, ev.vel, c.v (ACCENT)) * ev.gain, ev.gen); }
    void loopTick (const SampleData* smp, float drv, float dg, float dn, float& l, float& r) { lp.tick (smp, drv, dg, dn, l, r); }
    // No display of its own: with a sample loaded, the sample (its START and END to drag).
    int displayHeight() const override { return sample.get() != nullptr ? SampleStrip::kHeight : 0; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (auto s = sample.get()) strip.paint (g, SampleStrip::frameIn (r, 0), *s, *this, SSTART, SCUTS);
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        if (auto smp = sample.get(); smp != nullptr) { if (const auto st = SampleStrip::frameIn (r, 0); ! st.isEmpty()) { strip.mouseKit (p, SampleStrip::wave (st), down, *this, SSTART, SCUTS, *smp); return; } }
        if (down) strip.up();
    }
    void displayMouseUp() override { strip.up(); }
    void displayDouble (juce::Point<float> p, juce::Rectangle<float> r) override
    {
        if (auto smp = sample.get(); smp != nullptr) if (const auto st = SampleStrip::frameIn (r, 0); ! st.isEmpty()) SampleStrip::doubleKit (p, SampleStrip::wave (st), *this, SSTART, SCUTS, *smp);
    }
private:
    SampleStrip strip;
    double sr = 48000.0, osr = 96000.0;
    std::shared_ptr<const WaveSet> waves;
    Halfband dec[2];
    std::array<Voice, 8> voices;
    EchoScheduler echoes;
    std::vector<NoteEvent> events;
    RoleOutput out;
    SampleSlot sample;
    Noise rng;
    int sliceHits = 0; Noise sliceRng;      // (SF-000) slices
};
ModulePtr makePERC() { return std::make_unique<PercModule>(); }

// =====================================================================================  RUMBLE
// Kick-driven low-end bed. Listens to SF KICK on the bus (or MIDI), excites a thump through a
// model (reverb / delay / sub drone / distorted / dub / sample), then darkens, dirties and
// ducks it around the kick.
class RumbleModule : public Module
{
public:
    // SSTART: the sample's START; END, FADE IN, FADE OUT follow (sampleRegionParams).
    enum { SIZE, DARK, GRIT, PUMP, GATE, DENS, FILT, LEVEL, MODEL, TUNE, SOURCE, PREDELAY, WIDTH, PSHAPE, ESPACE, ALLOCON, KEYFOLLOW, SSTART, SGAIN = SSTART + 4, SNORM, SSLICE, SCYCLE, SCUTS, PPOST = SCUTS + kCuts + 1 };     // (SF-000) GAIN, NORMALISE, SLICE, CYCLE, MARKERS (and the 32 after it), POCKET AFTER FX
    std::string id() const override { return "RUMBLE"; }
    bool instrument() const override { return true; }
    int latency() const override { return Pocket::kLatency; }
    double tailSeconds() const override { return 10.0; }
    std::vector<ParamSpec> params() const override
    {
        auto p = withCuts (withSlices (withSampleGain (withRegion ({ pct ("size", "SIZE", 0.6f).prim().dir (1),
                 pct ("dark", "DARK", 0.6f).prim(),
                 pct ("grit", "GRIT", 0.3f).prim().dir (1),
                 pct ("pump", "PUMP", 0.7f).prim(),
                 pct ("gate", "GATE", 1.0f).prim(),
                 pct ("density", "DENSITY", 0.0f).prim().dir (1),
                 bip ("filter", "FILTER").prim(),
                 levelParam (-6.0f).prim(),
                 choice ("model", "MODEL", { "REVERB", "DELAY", "SUB DRONE", "DISTORTED", "DUB BOOM", "SAMPLE" }, 0),
                 knob ("tune", "TUNE", -12.0f, 12.0f, 0.0f, "st").slow(),
                 choice ("source", "SOURCE", { "KICK BUS", "MIDI", "BOTH" }, 0).noPreset().forget(),
                 choice ("predelay", "PREDELAY", { "0", "1/32", "1/16", "1/8" }, 1),
                 pct ("width", "WIDTH", 0.3f).slow(),
                 pct ("pocket_shape", "POCKET SHAPE", 0.5f).slow(),
                 choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 0),
                 toggle ("alloc", "ALLOC", true).noPreset().forget(),
                 toggle ("key_follow", "KEY FOLLOW", true).noPreset().forget() }))));
        p.push_back (toggle ("pocket_post", "AFTER FX", false).noPreset());        // (SF-000) the pocket left to the host, to make after the lane's effects
        return p;
    }
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP RUMBLE ONE-SHOT (MODEL: SAMPLE)"; }
    void loadFiles (const juce::StringArray& f) override { if (f.size() > 0) sample.set (loadSampleFile (juce::File (f[0]), sr)); }
    void save (juce::ValueTree& t) const override { sample.save (t, "SAMPLE"); }
    void load (const juce::ValueTree& t) override { sample.load (t, "SAMPLE", sr); }
    void prepare (double rate, int mb) override
    {
        if (auto s = sample.get(); s != nullptr && std::abs (s->sr - rate) > 1 && s->flac.getSize() > 0) sample.set (loadSampleFlac (s->flac, s->name, rate, s->flacGain));
        sr = rate;
        out.prepare (rate, mb, ROLE_RUMBLE, "RUMBLE", true);
        fdn.prepare (rate);
        for (auto& d : dly) d.prepare ((int) (rate * 3));
        for (auto& d : pre) d.prepare ((int) (rate * 1));
        os.prepare (rate, 2);
        midiDelay.reserve (256);
        extras.reserve (256);
        reset();
    }
    void reset() override
    {
        out.reset();
        fdn.clear();
        for (auto& d : dly) d.clear();
        for (auto& d : pre) d.clear();
        os.reset();
        for (auto& f : lp) for (auto& x : f) x.reset();
        for (auto& f : hp) f.reset();
        midiDelay.clear(); extras.clear();
        lastTrigPpq = -1e9;
        thumpT = 1e9f; sinceTrig = 1e9f; droneLevel = 0.0f; gateZ = 0.0f;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midi) override
    {
        const int model = c.ci (MODEL), src = c.ci (SOURCE);
        const double bps = c.beatsPerSample();
        const double secPerBeat = 60.0 / c.bpm;
        const double look = Pocket::kLatency * bps + (double) c.bus->d().pocketOffsetMs.load() * 0.001 / secPerBeat;
        const double lookSec = Pocket::kLatency / sr;
        // Transport jumped backwards (loop brace, restart): forget what was already triggered.
        if (c.ppq - look < lastTrigPpq - 0.25) { lastTrigPpq = c.ppq - look - 1e-6; midiDelay.clear(); extras.clear(); }
        // Gather triggers (as musical times on the delayed timeline).
        KickHit hits[kKickRing];
        const int nh = (src != 1 && c.bus->kickAlive (4000)) ? c.bus->recentKicks (hits, kKickRing) : 0;
        // MIDI notes are rendered one pocket-latency later, so everything shares the delayed timeline.
        if (src != 0)
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn() && midiDelay.size() < 250)
                    midiDelay.push_back ({ c.ppqAt (meta.samplePosition), meta.getMessage().getFloatVelocity() });
        fdn.set (c.v (SIZE), 0.6f + c.v (SIZE) * 5.0f, c.v (DARK), 0.15f, false, c.n);
        const float dlyTime = (float) (0.75 * c.samplesPerBeat());
        static const double preB[] = { 0.0, 0.125, 0.25, 0.5 };
        const float preS = (float) (preB[juce::jlimit (0, 3, c.ci (PREDELAY))] * c.samplesPerBeat());
        const float darkHz = expMap (1.0f - c.v (DARK), 70.0f, 600.0f);
        for (int k = 0; k < 2; ++k) { lp[k][0].set (darkHz, 0.5412f, sr); lp[k][1].set (darkHz, 1.3066f, sr); hp[k].set (26.0f, 0.7071f, sr); }
        const float grit = c.v (GRIT) + (model == 3 ? 0.6f : 0.0f);
        const float gateLen = c.v (GATE) >= 0.99f ? 1e9f : (float) ((0.1 + c.v (GATE) * 0.9) * c.samplesPerBeat());
        // KEY: the thump (and SUB DRONE) lands on the SF CONTROL key root; TUNE moves from there.
        float tuneSt = c.v (TUNE) + c.bend;         // (SF-000: and the pitch wheel)
        if (c.b (KEYFOLLOW) && c.keyRoot >= 0) tuneSt += keySnap (std::max (25.0f, lastF1), c.keyRoot);
        const float tune = semis (tuneSt);
        const float width = c.v (WIDTH);
        const float dens = c.v (DENS);
        const double space = echoSpaceBeats (c.ci (ESPACE));
        const SampleData* smp = sample.audio();
        nextReg = smp != nullptr ? regionFrom (c, SSTART, *smp, SGAIN) : SampleRegion();     // a thump from here on plays this part
        sliceSmp = smp; sliceAt = c.v (SSLICE); sliceCycle = c.ci (SCYCLE);                 // (SF-000) or the slice of it SLICE and CYCLE say
        sliceCuts = smp != nullptr ? cutsFrom (c, SCUTS, *smp) : Cuts();
        const float modelCal = cal::at (cal::rumble, model);
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        float gates[Oversampler::kBlock];
        const int n = std::min (c.n, Oversampler::kBlock);
        for (int i = 0; i < n; ++i)
        {
            const double tq = c.ppqAt (i) - look;
            // Kick-bus triggers whose time has come on the delayed timeline.
            for (int h = 0; h < nh; ++h)
                if (hits[h].ppq > lastTrigPpq + 1e-6 && hits[h].ppq <= tq && tq - hits[h].ppq < 0.25
                    && Bus::kickCurrent (hits[h], tq - hits[h].ppq, secPerBeat, lookSec))
                {
                    lastTrigPpq = hits[h].ppq;
                    fire (hits[h].f0, hits[h].f1, hits[h].tau, hits[h].vel, tune, tq, dens, space);
                }
            for (size_t k = 0; k < midiDelay.size();)
                if (midiDelay[k].first <= tq) { fire (200.0f, 50.0f, 0.012f, midiDelay[k].second, tune, tq, dens, space); midiDelay.erase (midiDelay.begin() + (long) k); }
                else ++k;
            for (size_t k = 0; k < extras.size();)
                if (extras[k].first <= tq) { fire (lastF0, lastF1, lastTau, extras[k].second, tune, -1.0, 0.0f, space); extras.erase (extras.begin() + (long) k); }
                else ++k;

            // Exciter.
            float x = 0.0f, xs = 0.0f;          // (SF-000) xs: a stereo sample's left less its right, halved (x is their sum, halved)
            if (thumpT < 0.4f)
            {
                if (model == 5 && smp != nullptr)
                {
                    const int len = sReg.end > 0.0 ? std::min (smp->length(), sReg.last()) : smp->length();
                    if ((int) sPos + 2 < len)
                    {
                        const float a = smp->read (0, sPos, tune), b = smp->wide ? smp->read (1, sPos, tune) : a;
                        const float g = thumpVel * sReg.gain (sPos);
                        x = 0.5f * (a + b) * g;
                        if (smp->wide) xs = 0.5f * (a - b) * g;
                    }
                    sPos += tune;
                }
                else
                {
                    const float f = thF1 + (thF0 - thF1) * std::exp (-thumpT / thTau);
                    thPh += f / (float) sr; if (thPh > 1.0f) thPh -= 1.0f;
                    x = std::sin (kTwoPi * thPh) * std::exp (-thumpT / 0.12f) * thumpVel;
                }
                thumpT += 1.0f / (float) sr;
            }
            ++sinceTrig;
            float wl = 0, wr = 0;
            pre[0].push (x);
            const float xp = preS > 1 ? pre[0].read (preS) : x;
            float xsp = 0.0f;
            if (model == 5) { pre[1].push (xs); xsp = preS > 1 ? pre[1].read (preS) : xs; }
            switch (model)
            {
                case 0: case 3: fdn.tick (xp, xp, wl, wr, false); wl *= 2.0f; wr *= 2.0f; break;
                case 1:
                {
                    const float yl = dly[0].read (dlyTime), yr = dly[1].read (dlyTime * 1.02f);
                    dly[0].push (xp + yr * (0.3f + c.v (SIZE) * 0.55f)); dly[1].push (yl * (0.3f + c.v (SIZE) * 0.55f));
                    wl = yl + xp * 0.5f; wr = yr + xp * 0.5f;
                    break;
                }
                case 2:
                {
                    // (SF-000) every knob on the drone. SIZE: an octave under it. WIDTH: the right a little off the left, so they
                    // beat apart. PREDELAY: its swell that much later in the bar. DENSITY: its swell 1, 2, 4 or 8 times a bar.
                    dronePh += thF1 / (float) sr; if (dronePh > 1.0f) dronePh -= 1.0f;
                    dronePh2 += thF1 * (1.0f + width * 0.012f) / (float) sr; if (dronePh2 > 1.0f) dronePh2 -= 1.0f;
                    droneSub += thF1 * 0.5f / (float) sr; if (droneSub > 1.0f) droneSub -= 1.0f;
                    const double swellAt = (c.ppqAt (i) - preB[juce::jlimit (0, 3, c.ci (PREDELAY))]) * 0.25 * (double) (1 << juce::jlimit (0, 3, (int) (c.v (DENS) * 3.99f)));
                    const float swell = 0.5f * (0.6f + 0.4f * std::sin (kTwoPi * (float) (swellAt - std::floor (swellAt))));
                    const float under = std::sin (kTwoPi * droneSub) * c.v (SIZE) * 0.6f;
                    wl = (std::sin (kTwoPi * dronePh) + under) * swell * droneLevel;
                    wr = (std::sin (kTwoPi * dronePh2) + under) * swell * droneLevel;
                    droneLevel = std::min (1.0f, droneLevel + 1.0f / (float) sr);
                    break;
                }
                case 4:
                {
                    float rl, rr; fdn.tick (xp * 0.5f, xp * 0.5f, rl, rr, false);
                    const float yl = dly[0].read (dlyTime), yr = dly[1].read (dlyTime);
                    dly[0].push (xp + yl * 0.5f); dly[1].push (xp + yr * 0.5f);
                    wl = rl + yl; wr = rr + yr;
                    break;
                }
                default:
                {
                    // (SF-000) a sample: SIZE is the room behind it (and WIDTH that room's width); with SIZE at nought, the sample alone
                    // (SF-000) in stereo: the sample's own left and right, into the room and beside it
                    const float al = xp + xsp, ar = xp - xsp;
                    float rl, rr; fdn.tick (al, ar, rl, rr, false);
                    const float room = c.v (SIZE) * 1.5f;
                    wl = al + rl * room; wr = ar + rr * room;
                    break;
                }
            }
            L[i] = wl; R[i] = wr;
            const float gt = sinceTrig < gateLen ? 1.0f : 0.0f;
            gateZ = gt + 0.998f * (gateZ - gt);
            gates[i] = gateZ;
        }
        // GRIT, 2x oversampled. Always through the oversampler (no delay switching in or out);
        // below 2 % the grit fades in from exactly clean.
        {
            const float g = 1.0f + grit * 8.0f, nrm = 1.0f / std::sqrt (g), w = std::min (1.0f, grit / 0.02f);
            os.process (0, L, n, [g, nrm, w] (float x) { return x + w * (fastTanh (x * g) * nrm - x); });
            os.process (1, R, n, [g, nrm, w] (float x) { return x + w * (fastTanh (x * g) * nrm - x); });
        }
        for (int i = 0; i < n; ++i)
        {
            const float wl = hp[0].tickHP (lp[0][1].tickLP (lp[0][0].tickLP (L[i])));
            const float wr = hp[1].tickHP (lp[1][1].tickLP (lp[1][0].tickLP (R[i])));
            const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * width;
            L[i] = (m + s) * gates[i] * modelCal;
            R[i] = (m - s) * gates[i] * modelCal;
        }
        out.process (c, io, FILT, -1, c.b (PPOST) ? 0.0f : c.v (PUMP), c.v (PSHAPE), c.b (ALLOCON), LEVEL, false);
    }
    // The pocket; under it, with a sample loaded, the sample (its START and END to drag).
    int displayHeight() const override { return kOwn + (sample.get() != nullptr ? SampleStrip::kHeight : 0); }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        const auto st = SampleStrip::frameIn (r, kOwn);
        if (auto s = sample.get(); s != nullptr && ! st.isEmpty()) strip.paint (g, st, *s, *this, SSTART, SCUTS);
        out.pocket.paint (g, r.withHeight ((float) kOwn));
    }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        if (auto smp = sample.get(); smp != nullptr) { if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty()) { strip.mouseKit (p, SampleStrip::wave (st), down, *this, SSTART, SCUTS, *smp); return; } }
        if (down) strip.up();
    }
    void displayMouseUp() override { strip.up(); }
    void displayDouble (juce::Point<float> p, juce::Rectangle<float> r) override
    {
        if (auto smp = sample.get(); smp != nullptr) if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty()) SampleStrip::doubleKit (p, SampleStrip::wave (st), *this, SSTART, SCUTS, *smp);
    }
private:
    static constexpr int kOwn = 80;     // the pocket
    SampleStrip strip;
    SampleRegion nextReg, sReg;         // SAMPLE: the part the next thump plays; the part this one plays
    const SampleData* sliceSmp = nullptr; float sliceAt = 0.0f; int sliceCycle = 0, sliceHits = 0; Noise sliceRng; Cuts sliceCuts;      // (SF-000) slices
    void fire (float f0, float f1, float tau, float vel, float tune, double tq, float dens, double space)
    {
        thF0 = f0 * tune; thF1 = std::max (25.0f, f1 * tune); thTau = std::max (0.002f, tau);
        sReg = nextReg;
        if (sliceSmp != nullptr && (sliceAt > 0.001f || sliceCycle != 0))      // (SF-000) hitRegion's rule, with what the block read
        {
            const int n = sliceCount (*sliceSmp, nextReg, &sliceCuts);
            int index = sliceAt > 0.001f ? std::min (n - 1, (int) (sliceAt * (float) n * 0.9999f)) : 0;
            if (sliceCycle == 1) index += sliceHits; else if (sliceCycle == 2) index += (int) (sliceRng.uni() * (float) n);
            sReg = sliceRegion (*sliceSmp, nextReg, index, &sliceCuts);
        }
        ++sliceHits;
        thumpT = 0.0f; thumpVel = vel; thPh = 0.0f; sPos = sReg.start; sinceTrig = 0;
        lastF0 = f0; lastF1 = f1; lastTau = tau;
        if (tq > 0.0 && dens > 0.001f && extras.size() < 240)
        {
            const float t1 = clamp01 (dens * 2.0f), t2 = clamp01 ((dens - 0.5f) * 2.0f);
            if (t1 > 0.01f) extras.push_back ({ tq + space, vel * 0.6f * t1 });
            if (t2 > 0.01f) { extras.push_back ({ tq + space * 0.5, vel * 0.4f * t2 }); extras.push_back ({ tq + space * 1.5, vel * 0.35f * t2 }); }
        }
    }
    double sr = 48000.0;
    RoleOutput out;
    Oversampler os;
    Fdn fdn;
    DelayLine dly[2], pre[2];           // (SF-000) pre[1]: a stereo sample's left less its right, beside it
    Svf lp[2][2], hp[2];
    SampleSlot sample;
    std::vector<std::pair<double, float>> midiDelay, extras;
    double lastTrigPpq = -1e9, sPos = 0;
    float dronePh2 = 0, droneSub = 0;       // (SF-000) the drone's right side, and the octave under it
    float thumpT = 1e9f, thumpVel = 1, thF0 = 200, thF1 = 50, thTau = 0.01f, thPh = 0, dronePh = 0, droneLevel = 0, gateZ = 1, lastF0 = 200, lastF1 = 50, lastTau = 0.01f;
    float sinceTrig = 1e9f;
};
ModulePtr makeRUMBLE() { return std::make_unique<RumbleModule>(); }
} // namespace sf
