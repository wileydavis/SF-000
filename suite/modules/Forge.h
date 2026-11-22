#pragma once
// (SF-000, the hundred and second build) KICK FORGE, the third engine every lane has. Until this build it was inside
// KICK (Drums.cpp); what makes its sound is here now, one piece of code, and both play it: KICK as it always did (its
// own voices, layer and split; its controls TUNE, DECAY, PUNCH, PITCH TIME, CLICK and DRIVE are the lane's), and every
// other lane through ForgeEngine (ForgeEngine.h), where those six are the Forge's own (fg_tune and the rest, appended
// last) and the note's pitch is the body's note. Moved, not changed: the kick's standing render (--forgeprint) comes
// out the same to the bit.
#include "Kit.h"

namespace sf
{
namespace forge
{
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
inline const ForgeShape kForge[4] = {
    //  ratio  tau     decay  fm    harm  drive impact impTau  impHp   ground groundT lp       clip  sub
    {   1.7f,  0.034f, 0.170f, 0.00f, 0.00f, 0.00f, 0.25f, 0.012f, 1200.0f, 0.45f, 0.55f,   700.0f, 0.8f, 1.00f },    // mellow, dubby
    {   5.2f,  0.007f, 0.055f, 0.55f, 0.22f, 0.20f, 0.90f, 0.022f,  500.0f, 0.18f, 0.25f, 14000.0f, 1.2f, 0.90f },    // punchy, tight
    {   3.0f,  0.018f, 0.240f, 0.25f, 0.12f, 0.30f, 0.65f, 0.035f,  350.0f, 0.90f, 0.95f,  5000.0f, 1.4f, 1.15f },    // boomy
    {   6.5f,  0.011f, 0.130f, 1.30f, 0.80f, 1.00f, 1.00f, 0.045f,  250.0f, 0.60f, 0.70f, 16000.0f, 2.2f, 1.00f } };  // distorted
// The shape at a place on CHARACTER: a curve through the four that passes each of them (Catmull-Rom), so the way from
// one to the next has no corner in it. Never below a tenth of the smallest of them (a curve can dip under its points).
inline ForgeShape forgeAt (float c)
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
inline const float kForgeTrim[11] = { 0.0f, -0.1f, 0.7f, 2.0f, 0.6f, -1.2f, -2.1f, -2.5f, -2.4f, -2.2f, -2.1f };
inline float forgeTrim (float c)
{
    const float x = std::min (std::max (c, 0.0f), 1.0f) * 10.0f;
    const int i = std::min (9, (int) x);
    return std::pow (10.0f, (kForgeTrim[i] + (kForgeTrim[i + 1] - kForgeTrim[i]) * (x - (float) i)) / 20.0f);
}

// (SF-000) KICK FORGE's GROUND: the body's tail in four delays that feed each other (41 to 80 ms, each darker as it
// goes round, nothing over 150 Hz going in), then rung at the kick's note, and held down while the body sounds. The
// module's, not a hit's: it rings on from one kick into the next. Asleep, it costs nothing.
struct Ground
{
    std::vector<float> d[4];
    int len[4] = { 1, 1, 1, 1 }, w[4] = {}, live = 0;
    float z[4] = {}, inLp = 0, inK = 0, damp = 0, fb = 0.4f, level = 0, levelTo = 0, levelK = 0, secNow = 0, feed = 1, darkNow = 1, depth = 1, sens = 1.6f, recNow = 1, width = 0, sideHp[4] = {}, sideK = 0, tilt = 0, tiltLp = 0, tiltK = 0, sign = 1, env = 0, rel = 0, dcX = 0, dcY = 0, dcR = 0;
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
        sideK = 1.0f - std::exp (-kTwoPi * 140.0f / (float) r);
        tiltK = 1.0f - std::exp (-kTwoPi * 120.0f / (float) r);
        darkNow = recNow = 1.0f;
        levelK = 1.0f - std::exp (-1.0f / (0.02f * (float) r));
        dcR = 1.0f - kTwoPi * 28.0f / (float) r;
        reset();
    }
    void reset()
    {
        for (auto& x : d) std::fill (x.begin(), x.end(), 0.0f);
        for (auto& x : z) x = 0.0f;
        inLp = env = dcX = dcY = tiltLp = 0.0f; live = 0;
        for (auto& x : sideHp) x = 0.0f;
        for (auto& x : w) x = 0;
        ring.ic1 = ring.ic2 = 0.0f;
    }
    // A hit: how long it rings (seconds to 60 dB down), how much of it, and the note it is rung at.
    // (feed: how much of the body goes in: less, for a ground drawn out longer, so that what it carries stays the same)
    void hit (float seconds, float amount, float hz, float feedIn = 1.0f)
    {
        fb = std::pow (10.0f, -3.0f * 0.0605f / std::max (0.08f, seconds));
        level = levelTo = amount; secNow = seconds; feed = feedIn;
        ring.set (hz, 5.0f, rate);
        live = (int) ((2.0 * (double) seconds + 1.0) * rate);
    }
    // While it rings: how long, and how much of it, as the knobs stand now (GROUND LENGTH and GROUND are heard on
    // a ground that is already ringing). Its level goes there in a fiftieth of a second; where the knobs have not
    // moved since the hit this changes nothing, to the bit.
    void follow (float seconds, float amount, float feedIn = 1.0f)
    {
        levelTo = amount; feed = feedIn;
        if (seconds == secNow) return;
        secNow = seconds;
        fb = std::pow (10.0f, -3.0f * 0.0605f / std::max (0.08f, seconds));
        live = std::max (live, (int) ((2.0 * (double) seconds + 1.0) * rate));      // (its length has moved: what is ringing is given the whole of the new one to die in, so a ground made longer is not cut off where the shorter would have ended)
    }
    bool awake() const { return live > 0; }
    // (SF-000, the ninety-ninth build) its colour and how it gives way to the body, as the knobs stand now. dark: its
    // cut-offs (what goes in, what each pass round keeps) divided by it; duck: how far it is held down while the body
    // sounds (0 to 1) and how readily (its follower's gain); recover: how long that follower takes to let go, times
    // it; spread: its width above 100 Hz. Each is worked out again only when it moves: as they come, nothing changes.
    void shade (float dark, float duckDepth, float duckSens, float recover, float spread)
    {
        if (dark != darkNow)
        {
            darkNow = dark;
            tilt = -0.5f * std::log2 (dark);         // (and a tilt on what it gives, about 120 Hz: -1, only what is under it; +1, what is over it twice)
            inK = 1.0f - std::exp (-kTwoPi * 150.0f / dark / (float) rate);
            damp = 1.0f - std::exp (-kTwoPi * 420.0f / dark / (float) rate);
        }
        if (recover != recNow) { recNow = recover; rel = std::exp (-1.0f / (0.07f * recover * (float) rate)); }
        depth = duckDepth; sens = duckSens; width = spread;
    }
    // One sample: in, the body as it is before its clipper; side, what it gives beside the middle (only with WIDTH).
    float tick (float in, float& side)
    {
        side = 0.0f;
        if (live <= 0) return 0.0f;
        if (--live == 0) { reset(); return 0.0f; }
        env = std::max (std::abs (in), env * rel);
        inLp += inK * ((feed != 1.0f ? in * feed : in) - inLp);
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
        float out = dcY;
        if (tilt != 0.0f) { tiltLp += tiltK * (dcY - tiltLp); out = dcY + tilt * (dcY - tiltLp); }
        const float open = 1.0f - depth * std::min (1.0f, env * sens);
        if (level != levelTo) { level += levelK * (levelTo - level); if (std::abs (levelTo - level) < 1.0e-6f) level = levelTo; }
        if (width > 0.0f)
        {
            // (two of its four passes against the other two: they are apart in time, so apart in sound; four one-pole
            // high-passes at 140 Hz, so that at the kick's note, about 50 Hz, it is 40 dB down: the floor stays in the middle)
            float sd = 0.5f * ((o[0] + o[2]) - (o[1] + o[3]));
            for (auto& h : sideHp) { h += sideK * (sd - h); sd -= h; }
            side = 16.0f * sd * width * level * open * open * sign;
        }
        return sign != 1.0f ? -(out * level * open * open) : out * level * open * open;
    }
};

// What a hit is made from, as the knobs stand. KICK reads them from its own controls; ForgeEngine from fg_ ones.
struct Knobs
{
    float ch = 0.3f, tone = 0.5f, punch = 0.5f, ptime = 0.5f, decay = 0.5f, click = 0.5f, drive = 0.2f;
    float impact = 0.5f, body = 0.5f, ground = 0.5f, pos = 0.1f, glen = 0.5f, low = 0.0f;
    float hang = 0.0f, curve = 0.5f, hold = 0.0f, hdec = 0.5f, phase = 0.0f;
    float ilen = 0.5f, ipitch = 0.0f, icut = 0.5f, iwidth = 0.5f, vary = 0.5f, solo = 0.0f;
    float gdark = 0.5f, gduck = 0.5f, grec = 0.5f, gwidth = 0.0f;
    bool flip = false;
};

// A Forge hit as it sounds (KICK's Voice is one, with what its other models need beside it).
struct Hit
{
    double t = 0.0, phase = 0.0;
    float vel = 1, f0 = 200, f1 = 50, tau = 0.01f, decay = 0.3f, click = 0, drive = 0, lp = 8000, fm = 0;
    float fade = 1.0f, mix = 1.0f;
    unsigned tick = 0;
    Svf res, clickBp;
    // the fundamental's last value (it bends itself), harmonics, the body's and the impact's level, the sub's, the
    // split's state and pace, the impact's place in the file, its two high-passes, its strength, how long it lasts, the
    // clipper's drive and what it is turned by after, and whether it feeds the ground
    float fgKeep = 0, fgKeepG = 1;      // LOW PRESERVE: how much, and the clean note's gain
    float fgCh = 0, fgEcho = 1;         // CHARACTER and the echo's darkening as they were at the hit (follow)
    // (the ninety-ninth build) the body's shape (HANG, CURVE's power, HOLD, HARM DECAY's times, POLARITY's sign), the
    // impact's (its end and where it starts fading, its read speed, IMPACT WIDTH's times, a hit that is the same every
    // time and its own noise for that), IMPACT ONLY, and a moment of the impact behind it, for the width it is given
    float fgHang = 0, fgCurve = 1, fgHold = 0, fgHdec = 1, fgSign = 1, fgImpEnd = 0.09f, fgImpFade0 = 0.07f, fgRate = 0.5f, fgIw = 1;
    bool fgLock = false, fgSolo = false, fgSide = false;
    Noise fgN;
    float fgDly[64] {}; int fgDi = 0;
    float fgPrev = 0, fgHarm = 0, fgBody = 1, fgImp = 0, fgSub = 1, fgLo = 0, fgLoK = 0, fgHpK = 0, fgHp1 = 0, fgHp2 = 0, fgImpTau = 0.006f, fgClip = 1, fgOut = 1, fgBright = 0;
    double fgPos = -1.0;
    float fgNoise = 1.0f;             // with no file: what the impact's noise, through its band, is turned by to be one strength
    // a stereo file's impact: its left less its right (halved) has the high-passes and the tone's low-pass of its own,
    // and takes the gain DRIVE and the clipper give the sum at each moment
    float fgHpS1 = 0, fgHpS2 = 0; Svf resS; bool fgWide = false;
    bool fgFeeds = false;
};

// The ground and what the last hit made it, before GROUND LENGTH and GROUND.
struct GroundUnit
{
    Ground g;
    float sec = 0.5f, amt = 0.0f, trim = 1.0f;
    // how long the ground rings: what CHARACTER and DECAY made it at the hit, by GROUND LENGTH (an eighth to eight times;
    // at its middle exactly that), twelve seconds at most
    float len (const Knobs& k) const { return std::min (12.0f, sec * std::pow (2.0f, (k.glen - 0.5f) * 6.0f)); }
    // ...and a ground drawn out longer than that is fed less of the body (by how much longer: measured, it then carries
    // about the same in all), so that GROUND LENGTH is how long and GROUND how much. Shorter, it is fed as it was.
    float feedFor (const Knobs& k) const { const float l = len (k); return l > sec ? sec / l : 1.0f; }
    // a ground that is ringing follows GROUND LENGTH and GROUND as they stand now (in the order the hit has it: the same
    // numbers to the bit)
    void follow (const Knobs& k) { if (g.awake()) g.follow (len (k), 2.0f * k.ground * amt * trim, feedFor (k)); }
    // (the ninety-ninth build) its colour and its give, as the knobs stand: DARK, DUCK, RECOVER, WIDTH
    void shade (const Knobs& k)
    {
        const float duck = k.gduck;
        g.shade (std::pow (2.0f, (k.gdark - 0.5f) * 4.0f),
                 duck <= 0.5f ? 2.0f * duck : 1.0f, duck <= 0.5f ? 1.6f : 1.6f * std::pow (4.0f, (duck - 0.5f) * 2.0f),
                 std::pow (2.0f, (k.grec - 0.5f) * 6.0f), k.gwidth);
    }
};

// (the ninety-ninth build) the body's pitch as it falls, against its note: how far there is still to fall (1 at the
// hit, 0 on the note). As it was, one curve; HANG keeps some of it in a stage eight times slower, CURVE bends it.
inline float fall (const Hit& v, float t)
{
    if (v.fgHang == 0.0f && v.fgCurve == 1.0f) return std::exp (-t / v.tau);
    const float x = v.fgCurve == 1.0f ? t / v.tau : std::pow (t / v.tau, v.fgCurve);
    return (1.0f - v.fgHang) * std::exp (-x) + v.fgHang * std::exp (-x * 0.125f);
}

// What shapes a hit after it is struck: TONE (its filter, its harmonics, the knock's bend), DRIVE, BODY and LOW
// PRESERVE. Taken at the hit, and again every block while it sounds, so that a ringing hit follows them; from the same
// numbers the same values, so a hit whose knobs have not moved is the hit it always was. CHARACTER, PUNCH, PITCH TIME,
// DECAY, CLICK, IMPACT and POSITION build the hit when it is struck: they are heard from the next.
inline void follow (const Knobs& k, Hit& v, const ForgeShape& f)
{
    const float tone = k.tone * 2.0f - 1.0f;
    v.fm = f.fm * (1.0f + 0.5f * tone);
    v.fgHarm = std::max (0.0f, f.harm + 0.5f * tone * (0.25f + f.harm));
    v.lp = std::min (18000.0f, f.lp * std::pow (4.0f, tone)) * v.fgEcho;
    v.fgBright = std::min (6000.0f, v.lp * 2.0f);                           // (the filter opens on the hit and closes onto v.lp)
    v.drive = clamp01 (f.drive * 0.7f + k.drive * 0.8f);
    v.fgBody = 2.0f * k.body;
    v.fgClip = f.clip * (1.0f + 1.5f * k.drive);
    v.fgOut = forgeTrim (v.fgCh) / fastTanh (std::min (v.fgClip, 8.0f)) * 0.8f;
    v.fgKeep = k.low;
    v.fgSolo = k.solo > 0.5f;
    // the clean note's gain: what the clipper gives the note at the moment its falling pitch comes to one and a half
    // times the kick's note (halfway into LOW PRESERVE's crossing), so it comes in at the level it was leaving the
    // clipper at (a sine driven a times over full scale into it leaves with a fundamental of about tanh a, rising to
    // 4/pi of the clipper's ceiling as it squares off); from there it dies away as the body does, where the clipper
    // would have held it up
    if (v.fgKeep > 0.0f)
    {
        float tn = v.f0 > 1.5f * v.f1 ? v.tau * std::log ((v.f0 - v.f1) / (0.5f * v.f1)) : 0.0f;
        if (tn > 0.0f && (v.fgHang != 0.0f || v.fgCurve != 1.0f))
        {
            // (HANG or CURVE: when the fall comes to one and a half times the note, found by halving)
            const float want = 0.5f * v.f1 / (v.f0 - v.f1);
            float lo = 0.0f, hi = 2.0f;
            for (int n = 0; n < 24; ++n) { const float m = 0.5f * (lo + hi); (fall (v, m) > want ? lo : hi) = m; }
            tn = hi;
        }
        const float e = std::min (1.0f, tn / 0.0004f) * std::exp (-std::max (0.0f, tn - v.fgHold) / v.decay) * v.fgBody * v.fgSub * v.vel;
        const float a = std::min (8.0f, v.fgClip * e);
        v.fgKeepG = e > 1.0e-4f ? v.fgOut * fastTanh (a) * (1.0f + 0.2732f * a * a / (1.0f + a * a)) / e : v.fgClip * v.fgOut;
    }
}

// A hit's settings, from CHARACTER and the knobs (the caller has set the pitch it lands on, v.f1, and its velocity;
// DECAY's and DENSITY's share of the decay is taken here). reg: the part of the file the impact may be taken from.
// main: the hit that rings the ground (the model's, not the layer's).
inline void start (const Knobs& k, Hit& v, int gen, const SampleData* smp, const SampleRegion* reg, bool main, Noise& rng, double osr, GroundUnit& gu)
{
    const float ch = k.ch;
    const ForgeShape f = forgeAt (ch);
    const float echo = std::pow (0.6f, (float) gen);
    v.fgCh = ch; v.fgEcho = echo;
    v.f0 = v.f1 * (1.0f + (f.ratio - 1.0f) * std::pow (2.0f, (k.punch - 0.5f) * 2.0f));
    v.tau = f.tau * std::pow (2.0f, (k.ptime - 0.5f) * 3.0f);
    v.decay = f.decay * std::pow (2.0f, (k.decay - 0.5f) * 4.0f) * echo;
    // (the ninety-ninth build) the body's shape, built with the hit
    v.fgHang = k.hang;
    v.fgCurve = std::pow (2.0f, (k.curve - 0.5f) * 2.0f);
    v.fgHold = 0.1f * k.hold * k.hold;
    v.fgHdec = std::pow (2.0f, (k.hdec - 0.5f) * 4.0f);
    v.fgSign = k.flip ? -1.0f : 1.0f;
    v.phase = 0.25 * (double) k.phase;
    v.fgSub = f.sub;
    v.fgLoK = 1.0f - std::exp (-kTwoPi * 120.0f / (float) osr);
    v.click = k.click * f.impact * 1.2f;            // (the pulse on the front of it: as much as the impact, so next to none on the mellow kick)
    follow (k, v, f);
    // the impact: from the lane's file at POSITION (ninety thousandths of a second of it, a little either side from hit
    // to hit), brought to one strength; or noise. VARIATION: how far one hit's differs from the next's (at its middle,
    // as it was; at nought not at all, the noise its own and the same every time; over the middle further in place and
    // level, and in pitch)
    const float vary = k.vary, a = 2.0f * vary;
    v.fgLock = vary <= 0.0f;
    if (v.fgLock) v.fgN = Noise();
    const float lvl = vary == 0.5f ? 0.9f + 0.2f * rng.uni() : v.fgLock ? 1.0f : 1.0f + a * (0.2f * rng.uni() - 0.1f);
    v.fgImp = 2.0f * k.impact * f.impact * lvl;
    // IMPACT LENGTH (its decay, and how long it is let sound: never less than as it was), PITCH, CUT
    const float lenMul = std::pow (2.0f, (k.ilen - 0.5f) * 4.0f);
    v.fgImpTau = f.impTau * lenMul;
    if (lenMul > 1.0f) { v.fgImpEnd = 0.09f * lenMul; v.fgImpFade0 = v.fgImpEnd - 0.02f * lenMul; }
    float ip = k.ipitch;
    if (a > 1.0f) ip += (a - 1.0f) * 2.0f * rng.next();
    const float ipMul = semis (ip);
    v.fgRate = 0.5f * ipMul;
    const float hp = f.impHp * std::pow (2.0f, (k.icut - 0.5f) * 4.0f);
    v.fgHpK = 1.0f - std::exp (-kTwoPi * hp / (float) osr);
    v.fgIw = 2.0f * k.iwidth;
    v.fgPos = -1.0;
    {
        // with no file POSITION is the noise's colour, a band from a low knock (250 Hz) to a bright tick (10 kHz), so the
        // knob is never one that does nothing
        const float hz = std::min (0.45f * (float) osr, 250.0f * std::pow (40.0f, k.pos) * ipMul);
        v.clickBp.set (hz, 1.6f, osr);
        v.fgNoise = 0.4f / (0.577f * std::sqrt (1.9635f * hz / (float) osr));
    }
    if (smp != nullptr && reg != nullptr && smp->length() > 4096 && v.fgImp > 0.0f)
    {
        const double span = (lenMul > 1.0f || ipMul != 1.0f ? 0.09 * std::max (1.0f, lenMul) * ipMul : 0.09) * smp->sr, room = std::max (0.0, reg->end - reg->start - span - 8.0);
        const double jit = vary == 0.5f ? (double) rng.next() * 0.0015 * smp->sr : v.fgLock ? 0.0 : (double) rng.next() * (a <= 1.0f ? 0.0015 * a : 0.0015 + 0.0185 * (a - 1.0f)) * smp->sr;
        double at = reg->start + (double) k.pos * room + jit;
        at = std::min (std::max (at, reg->start), reg->start + room);
        // (its strength: the grain's own over what will sound of it, with its lows out as they will be when it plays)
        double sum = 0.0;
        const int n0 = (int) at, n1 = std::min (smp->length() - 2, n0 + (int) (0.03 * smp->sr));
        const float* l = smp->buf.getReadPointer (0); const float* r = smp->buf.getReadPointer (smp->buf.getNumChannels() > 1 ? 1 : 0);
        const float hk = 1.0f - std::exp (-kTwoPi * hp / (float) smp->sr);
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
        v.fgWide = smp->wide;             // a stereo file: the impact keeps its left and right
    }
    v.fgSide = v.fgWide || v.fgIw > 1.0f;
    v.fgFeeds = main;
    if (main && gen == 0)
    {
        gu.sec = f.groundT * std::pow (2.0f, (k.decay - 0.5f) * 2.0f);
        gu.amt = f.ground; gu.trim = forgeTrim (ch);
        gu.g.hit (gu.len (k), 2.0f * k.ground * gu.amt * gu.trim, v.f1, gu.feedFor (k));
        gu.g.sign = v.fgSign;
    }
}

// One sample of a hit (at osr). side: a stereo impact's left less its right; groundIn: what the body gives the ground.
inline float voice (Hit& v, float t, const SampleData* s, float& side, float& groundIn, Noise& rng, double osr)
{
    // BODY: the wave starts at nought, rising, every hit (or where PHASE has it)
    const float f = v.f1 + (v.f0 - v.f1) * fall (v, t);
    const float w = kTwoPi * (float) v.phase;
    const float bend = v.fm * std::exp (-t / 0.03f);
    const float fund = std::sin (w + bend * v.fgPrev);
    v.fgPrev = fund;
    const float hE = v.fgHarm * std::exp (-t / ((0.22f * v.decay + 0.012f) * v.fgHdec));
    float body = fund;
    if (hE > 1.0e-4f) body += hE * (0.55f * std::sin (2.0f * w + 0.5f * bend * fund) + 0.30f * std::sin (3.0f * w));
    const float bodyEnv = std::min (1.0f, t / 0.0004f) * std::exp (-(v.fgHold > 0.0f ? std::max (0.0f, t - v.fgHold) : t) / v.decay) * v.fgBody;
    body *= bodyEnv;
    if (v.fgFeeds) groundIn += body * v.fade * v.mix * v.vel;
    // LOW PRESERVE: the share of the body's own note that goes round the mids' drive and the clipper, as the falling
    // pitch nears the note. Taken out of the body before the body is split into its lows and mids (that split is gentle,
    // and leaves a third of the note in the mids), and put back after the clipper. The ground has it all.
    float clean = 0.0f;
    if (v.fgKeep > 0.0f)
    {
        const float near = std::min (1.0f, std::max (0.0f, (2.5f - f / v.f1) / 1.5f));
        const float part = v.fgKeep * near * fund * bodyEnv;
        body -= part;
        clean = part * v.fgSub * v.vel;
    }
    if (v.fgSolo) { body = 0.0f; clean = 0.0f; }          // (IMPACT ONLY: the body heard no more, though it still feeds the ground)
    v.fgLo += v.fgLoK * (body - v.fgLo);
    float mid = body - v.fgLo;
    float sd = 0.0f;                    // a stereo file's impact: its left less its right, halved
    // IMPACT
    if (v.fgImp > 0.0f && t < v.fgImpEnd)
    {
        float x, xs = 0.0f;
        if (v.fgPos >= 0.0 && s != nullptr && (int) v.fgPos + 3 < s->length())
        {
            const float a = s->read (0, v.fgPos, v.fgRate), b = v.fgWide ? s->read (1, v.fgPos, v.fgRate) : a;
            x = 0.5f * (a + b);
            v.fgPos += v.fgRate;
            v.fgHp1 += v.fgHpK * (x - v.fgHp1); x -= v.fgHp1;       // (the file's lows are the body's to have)
            v.fgHp2 += v.fgHpK * (x - v.fgHp2); x -= v.fgHp2;
            if (v.fgWide)
            {
                xs = 0.5f * (a - b);
                v.fgHpS1 += v.fgHpK * (xs - v.fgHpS1); xs -= v.fgHpS1;
                v.fgHpS2 += v.fgHpK * (xs - v.fgHpS2); xs -= v.fgHpS2;
            }
        }
        else x = v.clickBp.tickBP (v.fgLock ? v.fgN.next() : rng.next()) * v.fgNoise;        // (the noise's band is its colour)
        if (v.fgIw != 1.0f)
        {
            // IMPACT WIDTH: its own side, made wider or narrower; over the middle, one made beside a mono impact from the
            // impact two thirds of a thousandth of a second before (it falls away in mono)
            xs *= std::min (v.fgIw, 1.0f) * (v.fgWide ? std::max (v.fgIw, 1.0f) : 1.0f);
            if (v.fgIw > 1.0f && ! v.fgWide) xs += (v.fgIw - 1.0f) * 0.5f * (x - v.fgDly[v.fgDi]);
            v.fgDly[v.fgDi] = x; v.fgDi = (v.fgDi + 1) & 63;
        }
        const float tail = clamp01 ((t - v.fgImpFade0) / (v.fgImpEnd > 0.09f ? 0.02f * v.fgImpEnd / 0.09f : 0.02f));   // (and out by its end, whatever its decay)
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
    const bool sideOn = v.fgSide && t < (v.fgImpEnd > 0.09f ? v.fgImpEnd + 0.11f : 0.2f);    // (its low-pass rings a little past the impact's end)
    if (v.lp < 15000.0f)
    {
        if ((v.tick++ & 15) == 0) { v.res.set (v.lp + v.fgBright * std::exp (-t / 0.03f), 0.7071f, osr); if (sideOn) { v.resS.g = v.res.g; v.resS.k = v.res.k; v.resS.a1 = v.res.a1; v.resS.a2 = v.res.a2; v.resS.a3 = v.res.a3; } }
        y = v.res.tickLP (y);
        if (sideOn) sd = v.resS.tickLP (sd);
    }
    {
        const float u = y * v.fgClip, sat = fastTanh (u);
        y = sat * v.fgOut;
        if (clean != 0.0f) y += clean * v.fgKeepG;
        side = sideOn ? sd * (std::abs (u) > 1.0e-4f ? sat / u : 1.0f) * v.fgClip * v.fgOut : 0.0f;
    }
    v.phase += f / osr;
    if (v.phase > 1.0) v.phase -= 1.0;
    v.t += 1.0 / osr;
    if (v.fgSign != 1.0f) { side = -side; return -y; }
    return y;
}
// whether a hit is over (as voice leaves it)
inline bool over (const Hit& v, float t) { return t > v.decay * 9.0f + 0.06f + v.fgHold; }
} // namespace forge
} // namespace sf
