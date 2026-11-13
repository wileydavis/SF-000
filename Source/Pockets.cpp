#include "Pockets.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sf0
{
namespace
{
constexpr float kMargin = 0.2f;         // a claim this much stronger than another's takes the band outright
constexpr float kBellQ = 3.5f;          // the bells: wide enough that a run of them cut alike makes a smooth dip
constexpr float kBankQ = 2.78f;         // two band-pass sections of this Q make a third-octave band
constexpr float kHoldFall = 10.0f;      // dB a second: how fast a band's remembered peak is let down
constexpr float kPi = 3.14159265358979f;

// log2, to about a thousandth of a dB once scaled (P. Mineiro's)
inline float fastLog2 (float x)
{
    union { float f; std::uint32_t i; } u = { x };
    const float y = (float) u.i * 1.1920928955078125e-7f - 126.94269504f;
    u.i = (u.i & 0x007FFFFFu) | 0x3f000000u;
    return y - 1.498030302f * u.f - 1.72587999f / (0.3520887068f + u.f);
}
inline float dbOf (float power)         { return 3.01029996f * fastLog2 (power + 1.0e-12f); }
inline float sstep (float e0, float e1, float x)
{
    const float t = std::min (1.0f, std::max (0.0f, (x - e0) / (e1 - e0)));
    return t * t * (3.0f - 2.0f * t);
}
inline float coef (float seconds, float tau) { return 1.0f - std::exp (-seconds / tau); }
}

float Pockets::pocketAt (float homeV, float widthV, float band)
{
    const float x = 2.0f * (band - homeBand (homeV)) / (3.0f * widthOct (widthV));     // 1 at the edge: half height
    const float x2 = x * x;
    return std::exp (-0.69314718f * x2 * x2);
}

float Pockets::gives (float mine, float theirs) { return sstep (0.0f, kMargin, theirs - mine); }

void Pockets::clearPart (Part& p)
{
    p = Part();
    for (int b = 0; b < kBands; ++b) { p.lev[b] = p.hold[b] = p.eff[b] = p.shown[b] = -120.0f; }
    p.bbHold = -120.0f; p.top = -120.0f; p.sinceOnset = 10.0f; p.armed = true; p.quiet = true;
}

void Pockets::prepare (double rate, int maxBlock)
{
    sr = rate;
    maxChunks = maxBlock / kChunk + 2;
    for (int b = 0; b < kBands; ++b)
    {
        const float f = std::min (bandHz ((float) b), 0.45f * (float) sr);
        const float t = std::tan (kPi * f / (float) sr), k = 1.0f / kBankQ;
        a1[b] = 1.0f / (1.0f + t * (t + k)); a2[b] = t * a1[b]; a3[b] = t * a2[b]; kk[b] = k;
        tb[b] = t;
        // a cut comes in over a few milliseconds, longer in the lows (a bell slammed shut on a slow wave clicks)
        attC[b] = juce::jlimit (0.001f, 0.006f, 0.25f / f);
        predictFor[b] = 3.5f / f;       // about what the band's two sections take to ring up
    }
    // How much of a bell's cut is felt one, two and three bands away (of a 6 dB cut: near enough for all)
    {
        const double A = std::pow (10.0, -6.0 / 40.0), Q = kBellQ;
        for (int j = 1; j <= 3; ++j)
        {
            const double om = std::pow (2.0, (double) j / 3.0), u = (1.0 - om * om) * (1.0 - om * om);
            const double num = u + (om * A / Q) * (om * A / Q), den = u + (om / (A * Q)) * (om / (A * Q));
            phi[j - 1] = (float) (10.0 * std::log10 (num / den) / -6.0);
        }
    }
    // and how much of a pure tone the bank lets through one to five bands away from its own
    for (int j = 1; j <= 5; ++j)
    {
        const double x = std::pow (2.0, (double) j / 3.0) - std::pow (2.0, -(double) j / 3.0), one = 1.0 / (1.0 + kBankQ * kBankQ * x * x);
        leak[j - 1] = (float) (one * one);
    }
    for (auto& p : parts) clearPart (p);
    topC.assign ((size_t) (maxChunks * kBands), 0.0f);
    topCeil.assign ((size_t) (maxChunks * kBands), 0.0f);
    setKnown = false; live = false; carving = false;
    const juce::SpinLock::ScopedLockType g (seenLock);
    latest = Seen();
    for (auto& row : latest.level) row.fill (-120.0f);
    for (auto& row : latest.usual) row.fill (-120.0f);
}

void Pockets::tables (const Set& s)
{
    for (int l = 0; l < kLanes; ++l)
    {
        for (int b = 0; b < kBands; ++b)
        {
            pocket[l][b] = pocketAt (s.home[(size_t) l], s.width[(size_t) l], (float) b);
            claimW[l][b] = claimOf (s.hold[(size_t) l], pocket[l][b]);
            retW[l][b] = gives (0.0f, claimW[l][b]);
        }
        maxCut[l] = kMaxCut * juce::jlimit (0.0f, 1.0f, s.yield[(size_t) l]);
    }
    for (int l = 0; l < kLanes; ++l)
        for (int k = 0; k < kLanes; ++k)
            for (int b = 0; b < kBands; ++b)
                giveW[l][k][b] = l == k ? 0.0f : gives (claimW[l][b], claimW[k][b]);
}

// One chunk of a part through the bank: what came through each band, summed.
void Pockets::look (Part& p, const float* l, const float* r, int len)
{
    float bb = 0.0f;
    for (int i = 0; i < len; ++i) { const float x = 0.5f * (l[i] + r[i]); bb += x * x; }
    p.bbNow = bb / (float) len;
    if (bb < 1.0e-11f * (float) len && p.quiet) { std::memset (p.acc, 0, sizeof (p.acc)); return; }
    alignas (32) float s1[kBands], s2[kBands], s3[kBands], s4[kBands], acc[kBands];
    std::memcpy (s1, p.s1, sizeof (s1)); std::memcpy (s2, p.s2, sizeof (s2));
    std::memcpy (s3, p.s3, sizeof (s3)); std::memcpy (s4, p.s4, sizeof (s4));
    std::memset (acc, 0, sizeof (acc));
    for (int i = 0; i < len; ++i)
    {
        const float x = 0.5f * (l[i] + r[i]);
        for (int b = 0; b < kBands; ++b)
        {
            float v3 = x - s2[b];
            float v1 = a1[b] * s1[b] + a2[b] * v3;
            float v2 = s2[b] + a2[b] * s1[b] + a3[b] * v3;
            s1[b] = 2.0f * v1 - s1[b];
            s2[b] = 2.0f * v2 - s2[b];
            const float y = kk[b] * v1;
            v3 = y - s4[b];
            v1 = a1[b] * s3[b] + a2[b] * v3;
            v2 = s4[b] + a2[b] * s3[b] + a3[b] * v3;
            s3[b] = 2.0f * v1 - s3[b];
            s4[b] = 2.0f * v2 - s4[b];
            const float z = kk[b] * v1;
            acc[b] += z * z;
        }
    }
    std::memcpy (p.s1, s1, sizeof (s1)); std::memcpy (p.s2, s2, sizeof (s2));
    std::memcpy (p.s3, s3, sizeof (s3)); std::memcpy (p.s4, s4, sizeof (s4));
    std::memcpy (p.acc, acc, sizeof (acc));
}

// A part's levels after a chunk: each band now, its remembered peak, and how far it is sounding
// there and matters there (its claim, before anyone's pocket is asked).
void Pockets::level (Part& p, int len)
{
    const float dt = (float) len / (float) sr;
    const float inst = p.bbNow;
    // the whole sound's level, fast and slow: a hit is the fast one jumping clear of the slow one
    p.bbEnv += (inst > p.bbEnv ? coef (dt, 0.0005f) : coef (dt, 0.008f)) * (inst - p.bbEnv);
    p.bbSlow += coef (dt, 0.040f) * (inst - p.bbSlow);
    const float lbb = dbOf (p.bbEnv), lslow = dbOf (p.bbSlow);
    if (p.armed && lbb > lslow + 6.0f && lbb > -60.0f) { p.sinceOnset = 0.0f; p.armed = false; }
    else if (! p.armed && lbb < lslow + 1.5f) p.armed = true;
    // What it remembers of itself (its peak in each band, and over all of them) is let down only
    // while it sounds: a kick back after a break is known for what it was.
    const bool sounding = lbb > -60.0f;
    const float fall = sounding ? kHoldFall * dt : 0.0f;
    p.bbHold = std::max (lbb, p.bbHold - fall);
    const float drop = lbb - p.bbHold;          // how far the whole sound is below its late peak (0: at it)

    const float cA = coef (dt, 0.002f), cR = coef (dt, 0.015f), inv = 1.0f / (float) len;
    float top = -120.0f, most = 0.0f;
    for (int b = 0; b < kBands; ++b)
    {
        const float in = p.acc[b] * inv;
        p.e[b] += (in > p.e[b] ? cA : cR) * (in - p.e[b]);
        most = std::max (most, p.e[b]);
    }
    // A band of the bank lets a good deal of its neighbours through (a pure tone shows 9 dB down in
    // the next band, 18 in the one after). Taken as it comes, a sound would seem to be where it is
    // not, and claim it. So each band is given what is left once what its neighbours account for
    // is taken out: a tone is then in its own band, or shared by the two it lies between.
    {
        float qa[kBands + 10] = {}, qb[kBands + 10] = {};
        float* from = qa + 5; float* to = qb + 5;
        for (int b = 0; b < kBands; ++b) from[b] = p.e[b];
        for (int it = 0; it < 4; ++it)
        {
            // (each band from the round before's figures: the bands are worked out side by side)
            for (int b = 0; b < kBands; ++b)
            {
                const float s = leak[0] * (from[b - 1] + from[b + 1]) + leak[1] * (from[b - 2] + from[b + 2]) + leak[2] * (from[b - 3] + from[b + 3])
                              + leak[3] * (from[b - 4] + from[b + 4]) + leak[4] * (from[b - 5] + from[b + 5]);
                to[b] = std::max (0.0f, p.e[b] - s);
            }
            std::swap (from, to);
        }
        for (int b = 0; b < kBands; ++b) p.pw[b] = from[b];
    }
    for (int b = 0; b < kBands; ++b)
    {
        p.lev[b] = dbOf (p.pw[b]);
        p.hold[b] = std::max (p.lev[b], p.hold[b] - fall);
        top = std::max (top, p.hold[b]);
        p.shown[b] = std::max (p.lev[b], p.shown[b] - 45.0f * dt);
        p.cutShown[b] = std::max (p.gS[b], p.cutShown[b] - 30.0f * dt);
    }
    p.top = top;
    // (a sound heard for the first time has no peaks to go by yet: its claims come in over its first tenth of a second)
    const float young = sstep (0.0f, 0.12f, p.heardFor);
    for (int b = 0; b < kBands; ++b)
    {
        // The level as taken. A low band takes tens of milliseconds to ring up, and a kick is over
        // its hardest part by then: for that long after a hit the band is taken to be where the
        // hit's own remembered spectrum says it is, at the level the whole sound has now.
        float eff = p.lev[b];
        if (p.sinceOnset < predictFor[b])
        {
            const float u = p.sinceOnset / predictFor[b];
            eff = std::max (eff, p.hold[b] + drop - 40.0f * std::max (0.0f, (u - 0.7f) / 0.3f));
        }
        p.eff[b] = eff;
        const float now = sstep (-26.0f, -8.0f, eff - p.hold[b]);          // sounding there now, against its own peak there
        const float matters = sstep (-24.0f, -6.0f, p.hold[b] - top);      // a band that is part of its sound, against its loudest
        const float loud = sstep (-72.0f, -60.0f, p.hold[b]);              // and loud enough to be heard at all
        p.claim[b] = now * matters * loud * young;
    }
    p.sinceOnset += dt;
    // the long average, while it sounds: what AUTO goes by
    if (sounding)
    {
        p.heardFor = std::min (30.0f, p.heardFor + dt);
        const float c = dt / std::min (8.0f, std::max (dt, p.heardFor));
        for (int b = 0; b < kBands; ++b) p.avg[b] += c * (p.pw[b] - p.avg[b]);
    }
    const bool silent = most < 1.0e-11f && inst < 1.0e-11f;
    if (silent && ! p.quiet) { std::memset (p.s1, 0, sizeof (p.s1)); std::memset (p.s2, 0, sizeof (p.s2)); std::memset (p.s3, 0, sizeof (p.s3)); std::memset (p.s4, 0, sizeof (p.s4)); }
    p.quiet = silent;
}

// The bells. want: the cut asked of each band, in dB (0 or more). Bells beside one another share
// their cuts, so each is set to what its band still lacks once its neighbours' are counted; each
// then moves toward that (quickly down, at SPEED back), and is run only while it is cutting.
void Pockets::aim (Part& p, int len, const float* want)
{
    p.runs = 0;
    bool any = false;
    for (int b = 0; b < kBands; ++b) any = any || want[b] > 0.01f;
    if (! any && p.active == 0) return;
    float g[kBands + 6] = {};                   // (three spare each side: no edges to test for)
    float* const gb = g + 3;
    if (any)
    {
        for (int b = 0; b < kBands; ++b) gb[b] = 0.6f * want[b];
        for (int it = 0; it < 4; ++it)
            for (int b = 0; b < kBands; ++b)
            {
                const float s = phi[0] * (gb[b - 1] + gb[b + 1]) + phi[1] * (gb[b - 2] + gb[b + 2]) + phi[2] * (gb[b - 3] + gb[b + 3]);
                gb[b] = std::min (want[b], std::max (0.0f, want[b] - s));
            }
    }
    const float dt = (float) len / (float) sr, cRel = coef (dt, relMs * 0.001f);
    int active = 0;
    for (int b = 0; b < kBands; ++b)
    {
        float cur = p.gS[b];
        const float to = gb[b];
        cur += (to > cur ? coef (dt, attC[b]) : cRel) * (to - cur);
        if (cur < 0.02f && to <= 0.0f) cur = 0.0f;
        p.gS[b] = cur;
        if (cur <= 0.0f && ! p.on[b]) continue;
        if (! p.on[b]) { p.ic1[0][b] = p.ic1[1][b] = p.ic2[0][b] = p.ic2[1][b] = 0.0f; p.m1[b] = 0.0f; p.gSet[b] = -1.0f; }
        if (cur != p.gSet[b])
        {
            const float A = std::exp (-0.0575646273f * cur);       // 10 ^ (-cur / 40)
            const float k = 1.0f / (kBellQ * A), t = tb[b];
            p.c1[b] = 1.0f / (1.0f + t * (t + k)); p.c2[b] = t * p.c1[b]; p.c3[b] = t * p.c2[b];
            p.mTo[b] = k * (A * A - 1.0f);
            p.gSet[b] = cur;
        }
        p.run[p.runs++] = b;                    // (one on its way out is run this once more, down to nothing)
        p.on[b] = cur > 0.0f;
        if (p.on[b]) ++active;
    }
    p.active = active;
}

// A part's bells are in series, so they are run one after another; but one part's are nothing to
// another's. So the parts are taken four at a time, their eight channels laid side by side, and
// each round runs one bell of each of the four over the chunk at once.
namespace
{
#if defined (__GNUC__) || defined (__clang__)
 typedef float V4 __attribute__ ((vector_size (16), aligned (16)));
 inline V4 v4 (float a)                              { return V4 { a, a, a, a }; }
#else
 struct V4 { float v[4]; float& operator[] (int i) { return v[i]; } float operator[] (int i) const { return v[i]; } };
 inline V4 operator+ (V4 a, V4 b)                    { return { { a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3] } }; }
 inline V4 operator- (V4 a, V4 b)                    { return { { a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3] } }; }
 inline V4 operator* (V4 a, V4 b)                    { return { { a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3] } }; }
 inline V4 v4 (float a)                              { return { { a, a, a, a } }; }
#endif
}

void Pockets::cut (int first, int count, float* const* chans, int at, int len)
{
    // those with bells to run, the busiest first (so the four of a group have about as many rounds)
    int order[kParts], n = 0;
    for (int q = 0; q < count; ++q) if (parts[(size_t) (first + q)].runs > 0) order[n++] = q;
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && parts[(size_t) (first + order[j])].runs > parts[(size_t) (first + order[j - 1])].runs; --j) std::swap (order[j], order[j - 1]);
    const V4 two = v4 (2.0f);
    for (int g0 = 0; g0 < n; g0 += 4)
    {
        const int in = std::min (4, n - g0);
        Part* who[4] = {};
        float* x[8];
        for (int k = 0; k < 4; ++k)
        {
            if (k < in) who[k] = &parts[(size_t) (first + order[g0 + k])];
            for (int c = 0; c < 2; ++c) x[2 * k + c] = k < in ? chans[2 * order[g0 + k] + c] + at : scratch[c];
        }
        V4 buf[kChunk][2];
        for (int i = 0; i < len; ++i)
        {
            buf[i][0] = V4 { x[0][i], x[1][i], x[2][i], x[3][i] };
            buf[i][1] = V4 { x[4][i], x[5][i], x[6][i], x[7][i] };
        }
        const int rounds = who[0]->runs;
        const float inv = 1.0f / (float) len;
        for (int round = 0; round < rounds; ++round)
        {
            V4 i1[2], i2[2], c1[2], c2[2], c3[2], mm[2], dm[2];
            for (int h = 0; h < 2; ++h) i1[h] = i2[h] = c1[h] = c2[h] = c3[h] = mm[h] = dm[h] = v4 (0.0f);
            for (int k = 0; k < in; ++k)
            {
                auto& p = *who[k];
                if (p.runs <= round) continue;              // (no more of its own: it passes through this round untouched)
                const int b = p.run[round], h = k / 2;
                for (int c = 0; c < 2; ++c)
                {
                    const int j = 2 * (k & 1) + c;
                    i1[h][j] = p.ic1[c][b]; i2[h][j] = p.ic2[c][b];
                    c1[h][j] = p.c1[b]; c2[h][j] = p.c2[b]; c3[h][j] = p.c3[b];
                    mm[h][j] = p.m1[b]; dm[h][j] = (p.mTo[b] - p.m1[b]) * inv;
                }
            }
            V4 a1 = i1[0], a2 = i2[0], b1 = i1[1], b2 = i2[1], ma = mm[0], mb = mm[1];
            const V4 ca1 = c1[0], ca2 = c2[0], ca3 = c3[0], cb1 = c1[1], cb2 = c2[1], cb3 = c3[1], da = dm[0], db = dm[1];
            for (int i = 0; i < len; ++i)
            {
                const V4 xa = buf[i][0], xb = buf[i][1];
                const V4 va3 = xa - a2, vb3 = xb - b2;
                const V4 va1 = ca1 * a1 + ca2 * va3, vb1 = cb1 * b1 + cb2 * vb3;
                const V4 va2 = a2 + ca2 * a1 + ca3 * va3, vb2 = b2 + cb2 * b1 + cb3 * vb3;
                a1 = two * va1 - a1; b1 = two * vb1 - b1;
                a2 = two * va2 - a2; b2 = two * vb2 - b2;
                ma = ma + da; mb = mb + db;
                buf[i][0] = xa + ma * va1; buf[i][1] = xb + mb * vb1;
            }
            i1[0] = a1; i2[0] = a2; i1[1] = b1; i2[1] = b2;
            for (int k = 0; k < in; ++k)
            {
                auto& p = *who[k];
                if (p.runs <= round) continue;
                const int b = p.run[round], h = k / 2;
                for (int c = 0; c < 2; ++c) { const int j = 2 * (k & 1) + c; p.ic1[c][b] = i1[h][j]; p.ic2[c][b] = i2[h][j]; }
                p.m1[b] = p.mTo[b];
            }
        }
        for (int i = 0; i < len; ++i)
            for (int j = 0; j < 2 * in; ++j) x[j][i] = buf[i][j / 4][j % 4];
    }
}

void Pockets::lanes (float* const* ch, int n, const Set& s, bool watch)
{
    bool stillCutting = false;
    if (carving) for (int l = 0; l < kParts && ! stillCutting; ++l) stillCutting = parts[(size_t) l].active > 0;
    const bool wantCarve = s.amount > 0.0f || stillCutting;
    const bool wasLive = live;
    live = wantCarve || watch;
    carving = wantCarve;
    if (! live)
    {
        if (wasLive)
        {
            const juce::SpinLock::ScopedTryLockType g (seenLock);
            if (g.isLocked()) latest.live = false;
        }
        return;
    }
    if (! wasLive) for (auto& p : parts) clearPart (p);         // (what it knew is from another time)
    if (! setKnown || s.home != set.home || s.width != set.width || s.hold != set.hold || s.yield != set.yield) tables (s);
    set = s; setKnown = true;
    amount = juce::jlimit (0.0f, 1.0f, s.amount);
    clear = clearDb (s.clear);
    relMs = releaseMs (s.speed);
    retMax = kMaxCut * juce::jlimit (0.0f, 1.0f, s.returns);

    int chunk = 0;
    for (int at = 0; at < n; at += kChunk, ++chunk)
    {
        const int len = std::min ((int) kChunk, n - at);
        for (int l = 0; l < kLanes; ++l)
        {
            auto& p = parts[(size_t) l];
            look (p, ch[2 * l] + at, ch[2 * l + 1] + at, len);
            level (p, len);
        }
        for (int l = 0; l < kLanes; ++l)
        {
            auto& p = parts[(size_t) l];
            alignas (32) float want[kBands] = {};
            const float mc = maxCut[l];
            if (amount > 0.0f && mc > 0.0f && ! p.quiet)
            {
                for (int k = 0; k < kLanes; ++k)
                {
                    const auto& o = parts[(size_t) k];
                    if (k == l || o.quiet) continue;
                    const float* w = giveW[l][k];
                    const float least = o.top - 12.0f;          // (a weak band of the other's asks for no more room than its strong ones)
                    for (int b = 0; b < kBands; ++b)
                    {
                        // what it would take to sit CLEAR below the other's peak there, as far as the other claims the band
                        const float need = std::min (mc, std::max (0.0f, p.eff[b] - std::max (o.hold[b], least) + clear));
                        want[b] = std::max (want[b], w[b] * o.claim[b] * need);
                    }
                }
                for (int b = 0; b < kBands; ++b) want[b] *= amount;
            }
            aim (p, len, want);
        }
        cut (0, kLanes, ch, at, len);
        // what the returns will have to give way to in this chunk: in each band, the strongest claim on it
        if (chunk < maxChunks)
        {
            float* tc = topC.data() + (size_t) (chunk * kBands);
            float* tl = topCeil.data() + (size_t) (chunk * kBands);
            for (int b = 0; b < kBands; ++b)
            {
                float best = 0.0f, ceilDb = 0.0f, score = 0.0f;
                for (int k = 0; k < kLanes; ++k)
                {
                    const auto& o = parts[(size_t) k];
                    if (o.quiet) continue;
                    const float at = std::max (o.hold[b], o.top - 12.0f);
                    const float c = retW[k][b] * o.claim[b], sc = c * std::max (0.0f, at + 90.0f);
                    if (sc > score) { score = sc; best = c; ceilDb = at; }
                }
                tc[b] = best; tl[b] = ceilDb - clear;
            }
        }
    }
    publish();
}

void Pockets::returns (float* const* ch, int n)
{
    if (! live) return;
    int chunk = 0;
    for (int at = 0; at < n; at += kChunk, ++chunk)
    {
        const int len = std::min ((int) kChunk, n - at);
        for (int r = 0; r < 2; ++r)
        {
            auto& p = parts[(size_t) (kLanes + r)];
            look (p, ch[2 * r] + at, ch[2 * r + 1] + at, len);
            level (p, len);
            alignas (32) float want[kBands] = {};
            if (amount > 0.0f && retMax > 0.0f && chunk < maxChunks && ! p.quiet)
            {
                const float* tc = topC.data() + (size_t) (chunk * kBands);
                const float* tl = topCeil.data() + (size_t) (chunk * kBands);
                for (int b = 0; b < kBands; ++b) want[b] = amount * tc[b] * std::min (retMax, std::max (0.0f, p.eff[b] - tl[b]));
            }
            aim (p, len, want);
        }
        cut (kLanes, 2, ch, at, len);
    }
    publish();
}

void Pockets::publish()
{
    const juce::SpinLock::ScopedTryLockType g (seenLock);
    if (! g.isLocked()) return;                 // (the screen is reading: next buffer)
    for (int q = 0; q < kParts; ++q)
    {
        const auto& p = parts[(size_t) q];
        for (int b = 0; b < kBands; ++b)
        {
            latest.level[(size_t) q][(size_t) b] = p.shown[b];
            latest.cut[(size_t) q][(size_t) b] = p.cutShown[b];
            latest.usual[(size_t) q][(size_t) b] = dbOf (p.avg[b]);
        }
        latest.heardFor[(size_t) q] = p.heardFor;
    }
    latest.live = true;
}

void Pockets::place (const Seen& sn, const Set& now, Set& out, std::array<bool, kLanes>& placed)
{
    out = now;
    placed.fill (false);
    for (int l = 0; l < kLanes; ++l)
    {
        const auto& u = sn.usual[(size_t) l];
        if (sn.heardFor[(size_t) l] < 2.0f) continue;
        float top = -200.0f;
        for (int b = 0; b < kBands; ++b) top = std::max (top, u[(size_t) b]);
        if (top < -80.0f) continue;
        // how much of its sound is in each band (its power, down to 24 dB under its loudest band)
        float w[kBands], sum = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            w[b] = u[(size_t) b] > top - 24.0f ? std::pow (10.0f, (u[(size_t) b] - top) / 10.0f) : 0.0f;
            sum += w[b];
        }
        static const float widths[] = { 1.0f, 1.4f, 2.0f, 2.8f, 4.0f };
        for (float oct : widths)
        {
            const float widthV = widthValue (oct);
            float best = -1.0f, at = 0.0f;
            for (int c4 = 0; c4 <= 4 * (kBands - 1); ++c4)
            {
                const float c = 0.25f * (float) c4, hv = c / (float) (kBands - 1);
                float held = 0.0f;
                for (int b = 0; b < kBands; ++b) held += w[b] * pocketAt (hv, widthV, (float) b);
                if (held > best + 1.0e-6f) { best = held; at = c; }
            }
            out.home[(size_t) l] = at / (float) (kBands - 1);
            out.width[(size_t) l] = widthV;
            if (best >= 0.6f * sum) break;
        }
        placed[(size_t) l] = true;
    }
}

void Pockets::seen (Seen& s) const
{
    const juce::SpinLock::ScopedLockType g (seenLock);
    s = latest;
}
} // namespace sf0
