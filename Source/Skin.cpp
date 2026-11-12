#include "Skin.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sf0
{
using juce::Colour;
namespace
{
constexpr float kU = 5.46f;             // screen units to the model's millimetre (its module is 49.72 mm wide; the screen's is 272 units)

// The light stands to the upper left and behind, high up: shadows are short and fall down and to the right.
constexpr float kElev = 1.117f;         // 64 degrees up
const float kHx = -0.44f / std::sqrt (0.44f * 0.44f + 0.90f * 0.90f), kHy = -0.90f / std::sqrt (0.44f * 0.44f + 0.90f * 0.90f);     // toward the light, across the screen (y runs down)
const float kLx = kHx * std::cos (kElev), kLy = kHy * std::cos (kElev), kLz = std::sin (kElev), kTanE = std::tan (kElev);
constexpr float kAmb = 0.52f;           // how much of the light comes from everywhere
constexpr float kAo = 0.42f, kAoReach = 3.2f, kAoRange = 14.0f;     // the dimness at the foot of anything that stands up
constexpr float kSoft = 6.0f;           // how hard a shadow's edge is
// The eye is a little to the front of straight above: what stands up is seen a little from its
// front, by this much of its height.
constexpr float kTilt = 0.11f;

inline float lin (float c) { return std::pow (c, 2.2f); }
inline float shine (float c) { const float c2 = c * c, c4 = c2 * c2, c8 = c4 * c4, c16 = c8 * c8; return c16 * c16 * c4; }     // to the 36th: how tight a highlight is
inline float hashf (int x, int y, int k)
{
    auto h = (juce::uint32) x * 374761393u + (juce::uint32) y * 668265263u + (juce::uint32) k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xffff) / 32767.5f - 1.0f;
}

const juce::uint8* srgbTable()
{
    static juce::uint8 t[4097];
    static bool made = false;
    if (! made) { for (int i = 0; i <= 4096; ++i) t[i] = (juce::uint8) juce::roundToInt (255.0f * std::pow ((float) i / 4096.0f, 1.0f / 2.2f)); made = true; }
    return t;
}
inline float toS (float l) { return (float) srgbTable()[juce::jlimit (0, 4096, (int) (l * 4096.0f + 0.5f))] * (1.0f / 255.0f); }

float sdRound (float px, float py, juce::Rectangle<float> r, float rad)
{
    const float hx = r.getWidth() * 0.5f, hy = r.getHeight() * 0.5f;
    rad = std::min (rad, std::min (hx, hy));
    const float qx = std::abs (px - r.getCentreX()) - (hx - rad), qy = std::abs (py - r.getCentreY()) - (hy - rad);
    const float ax = std::max (qx, 0.0f), ay = std::max (qy, 0.0f);
    return std::sqrt (ax * ax + ay * ay) + std::min (std::max (qx, qy), 0.0f) - rad;
}

void blur (std::vector<float>& a, int w, int h, int r)
{
    if (r < 1) return;
    std::vector<float> t (a.size());
    const float n = 1.0f / (float) (2 * r + 1);
    for (int y = 0; y < h; ++y)
    {
        const float* in = &a[(size_t) (y * w)]; float* out = &t[(size_t) (y * w)];
        float sum = 0.0f;
        for (int k = -r; k <= r; ++k) sum += in[juce::jlimit (0, w - 1, k)];
        for (int x = 0; x < w; ++x) { out[x] = sum * n; sum += in[std::min (w - 1, x + r + 1)] - in[std::max (0, x - r)]; }
    }
    for (int x = 0; x < w; ++x)
    {
        float sum = 0.0f;
        for (int k = -r; k <= r; ++k) sum += t[(size_t) (juce::jlimit (0, h - 1, k) * w + x)];
        for (int y = 0; y < h; ++y) { a[(size_t) (y * w + x)] = sum * n; sum += t[(size_t) (std::min (h - 1, y + r + 1) * w + x)] - t[(size_t) (std::max (0, y - r) * w + x)]; }
    }
}

// what a light looks like when it is on: the palette's colour, brighter, a little toward white
Colour led (Colour c)
{
    if (c.getSaturation() < 0.15f) return Colour (0xffFFFDF6);                  // the white ones
    return c.withMultipliedSaturation (1.15f).withMultipliedBrightness (1.42f).interpolatedWith (juce::Colours::white, 0.08f);
}
} // namespace

// --------------------------------------------------------------------------------------- relief
Relief::Relief (float w, float h, float pxPerUnit, float margin, int f)
    : fine (std::max (1, f)), m (margin)
{
    // (a whole number of device pixels, so the picture can be laid pixel on pixel)
    const int dw = (int) std::ceil ((w + margin * 2.0f) * pxPerUnit), dh = (int) std::ceil ((h + margin * 2.0f) * pxPerUnit);
    pw = dw * fine; ph = dh * fine; s = pxPerUnit * (float) fine;
    const auto n = (size_t) (pw * ph);
    z.assign (n, 0.0f); cr.assign (n, 0.0f); cg.assign (n, 0.0f); cb.assign (n, 0.0f);
    gl.assign (n, 0.0f); gr.assign (n, 0.0f); so.assign (n, 0.0f); em.assign (n, 0.0f);
}

template <class Sdf>
void Relief::stamp (float x0, float y0, float x1, float y1, Sdf&& sdf, float top, float round, const Finish& f, bool dent, float dome, float across)
{
    const int ax = std::max (0, (int) std::floor ((x0 + m) * s) - 1), bx = std::min (pw - 1, (int) std::ceil ((x1 + m) * s) + 1);
    const int ay = std::max (0, (int) std::floor ((y0 + m) * s) - 1), by = std::min (ph - 1, (int) std::ceil ((y1 + m) * s) + 1);
    const float r = lin (f.colour.getFloatRed()), g = lin (f.colour.getFloatGreen()), b = lin (f.colour.getFloatBlue());
    for (int y = ay; y <= by; ++y)
        for (int x = ax; x <= bx; ++x)
        {
            const float d = sdf (((float) x + 0.5f) / s - m, ((float) y + 0.5f) / s - m);
            const float cov = juce::jlimit (0.0f, 1.0f, 0.5f - d * s);
            if (cov <= 0.0f) continue;
            float hz = top;
            if (round > 0.0f && -d < round) { const float t = round - std::max (-d, 0.0f); hz = top - round + std::sqrt (std::max (0.0f, round * round - t * t)); }
            if (dome != 0.0f) { const float out = juce::jlimit (0.0f, 1.0f, 1.0f + d / across); hz -= dome * out * out; }
            const auto i = (size_t) (y * pw + x);
            z[i] += (hz - z[i]) * cov;
            if (dent) continue;
            cr[i] += (r - cr[i]) * cov; cg[i] += (g - cg[i]) * cov; cb[i] += (b - cb[i]) * cov;
            gl[i] += (f.gloss - gl[i]) * cov; gr[i] += (f.grain - gr[i]) * cov; em[i] += (f.glow - em[i]) * cov;
            so[i] += (1.0f - so[i]) * cov;
        }
}

void Relief::slab (R r, float corner, float top, float round, const Finish& f, bool dent)
{
    stamp (r.getX(), r.getY(), r.getRight(), r.getBottom(), [r, corner] (float x, float y) { return sdRound (x, y, r, corner); }, top, round, f, dent);
}

void Relief::disc (float cx, float cy, float radius, float top, float round, const Finish& f, bool dent, float dome)
{
    stamp (cx - radius, cy - radius, cx + radius, cy + radius, [=] (float x, float y) { return std::sqrt ((x - cx) * (x - cx) + (y - cy) * (y - cy)) - radius; }, top, round, f, dent, dome, radius);
}

void Relief::ring (float cx, float cy, float r0, float r1, int count, float gap, float top, const Finish& f)
{
    const float pi = juce::MathConstants<float>::pi, per = 2.0f * pi / (float) count, mid = (r0 + r1) * 0.5f, half = (r1 - r0) * 0.5f;
    stamp (cx - r1, cy - r1, cx + r1, cy + r1, [=] (float x, float y)
    {
        const float dx = x - cx, dy = y - cy, rr = std::sqrt (dx * dx + dy * dy);
        float a = std::atan2 (-dx, dy);                         // from six o'clock, clockwise
        if (a < 0.0f) a += 2.0f * pi;
        const float off = std::abs (a - (std::floor (a / per) + 0.5f) * per);       // from the middle of the segment it is in
        return std::max (std::abs (rr - mid) - half, gap * 0.5f - rr * std::sin (per * 0.5f - off));
    }, top, 0.0f, f, false);
}

void Relief::letters (const juce::Image& alpha, R where, float top, const Finish& f)
{
    const juce::Image::BitmapData bd (alpha, juce::Image::BitmapData::readOnly);
    const float r = lin (f.colour.getFloatRed()), g = lin (f.colour.getFloatGreen()), b = lin (f.colour.getFloatBlue());
    const int ax = std::max (0, (int) std::floor ((where.getX() + m) * s)), bx = std::min (pw - 1, (int) std::ceil ((where.getRight() + m) * s));
    const int ay = std::max (0, (int) std::floor ((where.getY() + m) * s)), by = std::min (ph - 1, (int) std::ceil ((where.getBottom() + m) * s));
    for (int y = ay; y <= by; ++y)
        for (int x = ax; x <= bx; ++x)
        {
            const float u = ((((float) x + 0.5f) / s - m) - where.getX()) / where.getWidth() * (float) alpha.getWidth();
            const float v = ((((float) y + 0.5f) / s - m) - where.getY()) / where.getHeight() * (float) alpha.getHeight();
            const int iu = (int) u, iv = (int) v;
            if (iu < 0 || iv < 0 || iu >= alpha.getWidth() || iv >= alpha.getHeight()) continue;
            const float cov = bd.getPixelColour (iu, iv).getFloatAlpha();
            if (cov <= 0.0f) continue;
            const auto i = (size_t) (y * pw + x);
            z[i] += (top - z[i]) * cov;
            cr[i] += (r - cr[i]) * cov; cg[i] += (g - cg[i]) * cov; cb[i] += (b - cb[i]) * cov;
            gl[i] += (f.gloss - gl[i]) * cov; gr[i] += (f.grain - gr[i]) * cov; em[i] += (f.glow - em[i]) * cov;
            so[i] += (1.0f - so[i]) * cov;
        }
}

// How much of the light reaches a point: walk from it toward the light and see what stands in the way.
float Relief::shade (int x, int y, float from, float zmax) const
{
    float res = 1.0f;
    const float rise = kTanE / s;
    const int steps = (int) std::ceil ((zmax - from) / rise) + 1;
    for (int t = 1; t <= steps; ++t)
    {
        const int qx = x + juce::roundToInt (kHx * (float) t), qy = y + juce::roundToInt (kHy * (float) t);
        if (qx < 0 || qy < 0 || qx >= pw || qy >= ph) break;
        const float ray = from + 0.04f + (float) t * rise;
        if (ray >= zmax) break;
        const float clear = ray - z[(size_t) (qy * pw + qx)];
        if (clear <= 0.0f) return 0.0f;
        res = std::min (res, kSoft * clear * s / (float) t);
    }
    return juce::jlimit (0.0f, 1.0f, res);
}

juce::Image Relief::picture (As as) const
{
    const int W = pw, H = ph;
    const auto n = (size_t) (W * H);
    const bool glowOnly = as == As::glowOnly;
    float zmax = 0.0f;
    for (float v : z) zmax = std::max (zmax, v);

    // the light on the top of every point of the surface
    std::vector<float> lf (n, 1.0f), sp (n, 0.0f);
    if (! glowOnly)
    {
        std::vector<float> zb (z);
        const int br = std::max (1, juce::roundToInt (kAoReach * s));
        blur (zb, W, H, br); blur (zb, W, H, br);
        float hx = kLx, hy = kLy, hz = kLz + 1.0f;              // half way between the light and the eye
        { const float inv = 1.0f / std::sqrt (hx * hx + hy * hy + hz * hz); hx *= inv; hy *= inv; hz *= inv; }
        const float flat = shine (hz);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const auto i = (size_t) (y * W + x);
                float gx = (z[(size_t) (y * W + std::min (W - 1, x + 1))] - z[(size_t) (y * W + std::max (0, x - 1))]) * 0.5f * s;
                float gy = (z[(size_t) (std::min (H - 1, y + 1) * W + x)] - z[(size_t) (std::max (0, y - 1) * W + x)]) * 0.5f * s;
                // the shine comes off the shape itself; the grain only roughens how the light falls on it
                const float sInv = 1.0f / std::sqrt (gx * gx + gy * gy + 1.0f);
                const float gloss = std::max (0.0f, shine (std::max (0.0f, (-gx * hx - gy * hy + hz) * sInv)) - flat);
                if (gr[i] > 0.0f) { gx += gr[i] * hashf (x, y, 1); gy += gr[i] * hashf (x, y, 2); }
                const float inv = 1.0f / std::sqrt (gx * gx + gy * gy + 1.0f);
                const float nx = -gx * inv, ny = -gy * inv, nz = inv;
                const float diff = std::min (std::max (0.0f, nx * kLx + ny * kLy + nz * kLz) / kLz, 1.4f);
                const float sh = z[i] > zmax - 0.02f ? 1.0f : shade (x, y, z[i], zmax);      // (nothing stands over the highest things)
                const float ao = 1.0f - kAo * juce::jlimit (0.0f, 1.0f, (zb[i] - z[i]) / kAoRange);
                // (the falling-off of the light is the shape's own: the surface it stands on has its own)
                lf[i] = (kAmb * ao + (1.0f - kAmb) * diff * sh) * (1.0f + so[i] * fall * (0.5f - (0.35f * (float) x / (float) W + 0.65f * (float) y / (float) H)));
                sp[i] = gl[i] * gloss * sh;
            }
    }

    // What the eye sees at each pixel: the highest thing on its line of sight. A point at height h
    // stands kTilt * h up the screen from its foot.
    const float perStep = 1.0f / (kTilt * s);                   // the height one pixel up the screen is worth
    const int J = glowOnly ? 0 : (int) std::ceil (zmax / perStep);
    bool anyGlow = false;
    for (size_t i = 0; i < n && ! anyGlow; ++i) anyGlow = em[i] > 0.0f;
    const size_t ne = anyGlow ? n : 1;
    std::vector<float> oR (n), oG (n), oB (n), oS (n), oF (n, 1.0f), eR (ne, 0.0f), eG (ne, 0.0f), eB (ne, 0.0f);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            auto i = (size_t) (y * W + x);
            float light = lf[i], spec = sp[i];
            for (int j = J; j >= 1; --j)
            {
                if (y + j >= H) continue;
                const auto q = (size_t) ((y + j) * W + x);
                const float h = (float) j * perStep;
                if (z[q] < h) continue;
                i = q;
                if (z[q] < h + perStep) { light = lf[q]; spec = sp[q]; }                       // its top
                else { light = kAmb * (0.50f + 0.40f * juce::jlimit (0.0f, 1.0f, h / z[q])); spec = 0.0f; }      // its front, which the light does not reach
                break;
            }
            const auto o = (size_t) (y * W + x);
            const float glow = em[i] * so[i];
            oS[o] = so[i]; oF[o] = light;
            // (what gives off light is seen by that light, whatever falls on it)
            const float keep = glowOnly ? 0.0f : 1.0f - 0.75f * em[i];
            oR[o] = cr[i] * (light * keep + (1.0f - keep)) + spec;
            oG[o] = cg[i] * (light * keep + (1.0f - keep)) + spec;
            oB[o] = cb[i] * (light * keep + (1.0f - keep)) + spec;
            if (glow > 0.0f) { eR[o] = cr[i] * glow; eG[o] = cg[i] * glow; eB[o] = cb[i] * glow; }
        }
    if (anyGlow)
    {
        // the light it throws round itself
        const int br = std::max (1, juce::roundToInt (1.1f * s));
        for (auto* e : { &eR, &eG, &eB }) { blur (*e, W, H, br); blur (*e, W, H, br); blur (*e, W, H, br); }
    }

    // Down to the device's pixels. Where a shape is, its colour; elsewhere the picture only
    // darkens or lightens what is under it (shade, a dent's lit edge) or adds a glow.
    const int dw = W / fine, dh = H / fine;
    juce::Image img (juce::Image::ARGB, dw, dh, true);
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        const float each = 1.0f / (float) (fine * fine);
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x)
            {
                float pr = 0.0f, pg = 0.0f, pb = 0.0f, pa = 0.0f;       // premultiplied, in the screen's own values
                for (int fy = 0; fy < fine; ++fy)
                    for (int fx = 0; fx < fine; ++fx)
                    {
                        const auto o = (size_t) ((y * fine + fy) * W + x * fine + fx);
                        const float sol = oS[o], gR = anyGlow ? eR[o] : 0.0f, gG = anyGlow ? eG[o] : 0.0f, gB = anyGlow ? eB[o] : 0.0f;
                        // under: the surface below, darkened (black) or lightened (white)
                        float ua = 0.0f, uc = 0.0f;
                        if (oF[o] < 1.0f) ua = 1.0f - toS (std::max (0.0f, oF[o]));
                        else if (oF[o] > 1.0f) { ua = std::min (0.55f, (oF[o] - 1.0f) * 1.1f); uc = 1.0f; }
                        float ur = uc * ua, ug = uc * ua, ub = uc * ua;
                        if (const float gm = std::max (gR, std::max (gG, gB)); gm > 0.0005f)
                        {
                            const float ga = std::min (1.0f, gm * 1.6f);
                            ur = toS (gR / gm) * ga + ur * (1.0f - ga); ug = toS (gG / gm) * ga + ug * (1.0f - ga); ub = toS (gB / gm) * ga + ub * (1.0f - ga);
                            ua = ga + ua * (1.0f - ga);
                        }
                        pr += (sol * toS (std::min (1.0f, oR[o] + gR)) + (1.0f - sol) * ur) * each;
                        pg += (sol * toS (std::min (1.0f, oG[o] + gG)) + (1.0f - sol) * ug) * each;
                        pb += (sol * toS (std::min (1.0f, oB[o] + gB)) + (1.0f - sol) * ub) * each;
                        pa += (sol + (1.0f - sol) * ua) * each;
                    }
                auto* px = (juce::PixelARGB*) bd.getPixelPointer (x, y);
                px->setARGB ((juce::uint8) juce::jlimit (0, 255, juce::roundToInt (pa * 255.0f)), (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (std::min (pr, pa) * 255.0f)),
                             (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (std::min (pg, pa) * 255.0f)), (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (std::min (pb, pa) * 255.0f)));
            }
    }
    return img;
}

// ----------------------------------------------------------------------------------------- skin
namespace
{
enum What { wPanel = 1, wKnob, wRing, wKey, wLamp, wHollow, wBadge };
const Colour kCase (0xffA4B1B7), kBlack (0xff2B2B28), kUnlit (0xff4D514E), kGreen (0xff428F47), kAmber (0xffC5843F);
// the dark instrument's own colours: the case, a knob's base, its unlit lights, its cap, an unlit lamp, the name plate
const Colour kNightCase (0xff24272A), kNightBase (0xff111211), kNightUnlit (0xff474B48), kNightCap (0xff232321), kNightLens (0xff3C403D), kNightBadge (0xff151615);
}

bool Skin::emits (Colour c) { return c == kGreen || c == kAmber; }

void Skin::begin (G& g)
{
    const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (scale <= 0.0f) { scale = want = sc; return; }
    if (std::abs (sc - scale) < 0.002f) { want = scale; return; }
    if (std::abs (sc - want) > 0.002f) { want = sc; wantAt = juce::Time::getMillisecondCounter(); }
}

bool Skin::settled()
{
    if (want == scale || juce::Time::getMillisecondCounter() - wantAt < 300) return false;
    scale = want;
    for (auto& p : pics) p.clear();
    for (auto& c : caseImg) c = {};
    return true;
}

const Skin::Pic& Skin::pic (const Key& k, const std::function<Pic()>& make)
{
    auto& mine = pics[dark];
    const auto it = mine.find (k);
    if (it != mine.end()) return it->second;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto& made = mine.emplace (k, make()).first->second;
    if (std::getenv ("SF000_SKIN_TIMES") != nullptr)
        std::printf ("      picture %d (%d x %d, state %d): %d x %d px, %.1f ms\n", std::get<0> (k), std::get<1> (k), std::get<2> (k), std::get<4> (k), made.img.getWidth(), made.img.getHeight(), juce::Time::getMillisecondCounterHiRes() - t0);
    return made;
}

Skin::Pic Skin::from (const Relief& r, Relief::As as) const { return { r.picture (as), -r.margin(), -r.margin(), scale }; }

void Skin::put (G& g, const Pic& p, float x, float y) const
{
    g.drawImage (p.img, R (x + p.ox, y + p.oy, (float) p.img.getWidth() / p.s, (float) p.img.getHeight() / p.s));
}

// The case: one slab, its top edge rounded over, its front seen below it.
void Skin::body (G& g, float w, float h)
{
    auto& shell = caseImg[dark];
    const Colour tint = dark ? kNightCase : kCase;
    if (shell.isNull())
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const struct Timed { double t0; ~Timed() { if (std::getenv ("SF000_SKIN_TIMES") != nullptr) std::printf ("      the case: %.1f ms\n", juce::Time::getMillisecondCounterHiRes() - t0); } } timed { t0 };
        const float S = scale;
        const int W = juce::roundToInt (w * S), H = juce::roundToInt (h * S);
        shell = juce::Image (juce::Image::ARGB, W, H, false);
        juce::Image::BitmapData bd (shell, juce::Image::BitmapData::writeOnly);
        const R top (8.0f, 12.0f, w - 16.0f, h - 25.0f), swept = top.withBottom (top.getBottom() + kTilt * 25.4f * kU);
        const float rad = 26.0f, round = 4.5f, cr = lin (tint.getFloatRed()), cg = lin (tint.getFloatGreen()), cb = lin (tint.getFloatBlue());
        float hx = kLx, hy = kLy, hz = kLz + 1.0f;
        { const float inv = 1.0f / std::sqrt (hx * hx + hy * hy + hz * hz); hx *= inv; hy *= inv; hz *= inv; }
        const float flat = shine (hz), grain = dark ? 0.055f : 0.11f;
        for (int y = 0; y < H; ++y)
        {
            const float uy = ((float) y + 0.5f) / S;
            for (int x = 0; x < W; ++x)
            {
                const float ux = ((float) x + 0.5f) / S;
                float r = 0.0f, gg = 0.0f, b = 0.0f;
                const float inX = std::min (ux - top.getX(), top.getRight() - ux), inY = std::min (uy - top.getY(), top.getBottom() - uy);
                const bool deep = inX > rad && inY > rad;                                       // well inside: flat
                const float d = deep ? -rad : sdRound (ux, uy, top, rad);
                const float cov = juce::jlimit (0.0f, 1.0f, 0.5f - d * S);
                if (cov < 1.0f)
                {
                    const float covW = juce::jlimit (0.0f, 1.0f, 0.5f - sdRound (ux, uy, swept, rad) * S);
                    const float l = kAmb * (0.62f - 0.34f * juce::jlimit (0.0f, 1.0f, (uy - top.getBottom()) / (swept.getBottom() - top.getBottom())));
                    r = cr * l * covW; gg = cg * l * covW; b = cb * l * covW;
                }
                const float fall = 1.035f - 0.07f * (0.35f * ux / w + 0.65f * uy / h);            // the light is nearer the top left
                if (deep)
                {
                    // flat: only the grain turns the light (to first order, with no roots or powers: most of the picture is this)
                    const float l = (kAmb + (1.0f - kAmb) * (1.0f - grain * (hashf (x, y, 1) * kLx + hashf (x, y, 2) * kLy) / kLz)) * fall;
                    r = cr * l; gg = cg * l; b = cb * l;
                }
                else if (cov > 0.0f)
                {
                    float gx = grain * hashf (x, y, 1), gy = grain * hashf (x, y, 2);            // bead-blasted
                    float spec = 0.0f;
                    if (d > -round)
                    {
                        // the edge rolls over
                        const float e = std::min (0.97f, (d + round) / round), slope = e / std::sqrt (1.0f - e * e);
                        float dx = sdRound (ux + 0.25f, uy, top, rad) - sdRound (ux - 0.25f, uy, top, rad), dy = sdRound (ux, uy + 0.25f, top, rad) - sdRound (ux, uy - 0.25f, top, rad);
                        const float len = std::sqrt (dx * dx + dy * dy) + 1.0e-6f;
                        const float ex = -slope * dx / len, ey = -slope * dy / len;         // (the height falls outward)
                        spec = 0.22f * std::max (0.0f, shine (std::max (0.0f, (-ex * hx - ey * hy + hz) / std::sqrt (ex * ex + ey * ey + 1.0f))) - flat);       // the shine off the roll itself
                        gx += ex; gy += ey;
                    }
                    const float inv = 1.0f / std::sqrt (gx * gx + gy * gy + 1.0f), nx = -gx * inv, ny = -gy * inv, nz = inv;
                    const float diff = std::min (std::max (0.0f, nx * kLx + ny * kLy + nz * kLz) / kLz, 1.4f);
                    const float l = (kAmb + (1.0f - kAmb) * diff) * fall;
                    r += (cr * l + spec - r) * cov; gg += (cg * l + spec - gg) * cov; b += (cb * l + spec - b) * cov;
                }
                auto* px = (juce::PixelARGB*) bd.getPixelPointer (x, y);
                px->setARGB (255, (juce::uint8) juce::roundToInt (toS (std::min (1.0f, r)) * 255.0f), (juce::uint8) juce::roundToInt (toS (std::min (1.0f, gg)) * 255.0f),
                             (juce::uint8) juce::roundToInt (toS (std::min (1.0f, b)) * 255.0f));
            }
        }
    }
    g.setOpacity (1.0f);
    g.drawImage (shell, R (0.0f, 0.0f, w, h));
}

void Skin::makePanel (float width, float height, Colour ground) { if (scale > 0.0f) panelPic (juce::roundToInt (width), juce::roundToInt (height), ground); }

const Skin::Pic& Skin::panelPic (int w, int h, Colour ground)
{
    return pic (Key { wPanel, w, h, ground.getARGB(), 0 }, [&]
    {
        Relief rf ((float) w, (float) h, scale, 10.0f);
        const R b (0.0f, 0.0f, (float) w, (float) h);
        rf.slab (b.expanded (1.3f), 8.6f, -2.6f, 0.0f, { (dark ? kNightCase : kCase).darker (0.55f), 0.0f });     // the pocket it stands in, seen as the gap round it
        rf.slab (b, 7.5f, 1.27f * kU, 1.2f, { ground, dark ? 0.22f : 0.10f, dark ? 0.05f : 0.10f });       // (a dark surface shows its grain more)
        rf.fall = 0.05f;
        return from (rf);
    });
}

void Skin::panel (G& g, R r, Colour ground)
{
    g.setOpacity (1.0f);
    put (g, panelPic (juce::roundToInt (r.getWidth()), juce::roundToInt (r.getHeight()), ground), r.getX(), r.getY());
}

namespace
{
struct KnobDims { float box, base, r0, r1, cap, tall, round; };
KnobDims dims (int size)
{
    // from the model: the big knob's base is 7.15 mm across, its ring 2.29 to 3.37 mm out, its cap 4.07 mm across and 5.08 mm tall
    if (size >= 40) return { 20.0f, 19.5f, 12.5f, 18.4f, 11.1f, 5.08f * kU, 0.45f * kU };
    return { 15.0f, 13.65f, 8.75f, 12.9f, 8.3f, 3.56f * kU, 0.38f * kU };
}
}

void Skin::knob (G& g, float cx, float cy, int size)
{
    const auto d = dims (size);
    const auto& p = pic (Key { wKnob, size >= 40 ? 40 : 30, 0, 0, 0 }, [&]
    {
        Relief rf (d.box * 2.0f, d.box * 2.0f, scale, 12.0f, 3);
        rf.disc (d.box, d.box, d.base, 0.15f, 0.3f, { dark ? kNightBase : Colour (0xff1E1F1D), 0.55f });      // the dark ring let into the plate
        rf.ring (d.box, d.box, d.r0, d.r1, 16, 1.5f, 0.25f * kU, { dark ? kNightUnlit : kUnlit, 0.45f });
        rf.disc (d.box, d.box, d.cap + 0.7f, 0.03f * kU + 0.9f, 0.5f, { dark ? kNightCap.darker (0.2f) : kBlack, 0.25f, 0.06f });   // the cap's skirt
        rf.disc (d.box, d.box, d.cap, d.tall, d.round, { dark ? kNightCap : kBlack.brighter (0.12f), dark ? 0.75f : 0.42f, 0.045f }, false, 1.1f);
        return from (rf);
    });
    g.setOpacity (1.0f);
    put (g, p, cx - d.box, cy - d.box);
}

void Skin::ringLit (G& g, float cx, float cy, int size, float a, float b, Colour c)
{
    const auto d = dims (size);
    const auto& p = pic (Key { wRing, size >= 40 ? 40 : 30, 0, c.getARGB(), 0 }, [&]
    {
        Relief rf (d.box * 2.0f, d.box * 2.0f, scale, 6.0f, 3);
        rf.ring (d.box, d.box, d.r0, d.r1, 16, 1.5f, 0.25f * kU, { led (c), 0.0f, 0.0f, 1.0f });
        return from (rf, Relief::As::glowOnly);
    });
    // Whole segments are lit whole; the one the value stands in is as bright as the value is far into it.
    const float pi = juce::MathConstants<float>::pi, reach = d.box + 6.0f;
    auto wedge = [&] (float t0, float t1, float bright)
    {
        juce::Path w;
        w.addPieSegment (cx - reach, cy - reach, reach * 2.0f, reach * 2.0f, pi + t0 * 2.0f * pi, pi + t1 * 2.0f * pi, 0.0f);
        const G::ScopedSaveState state (g);
        g.reduceClipRegion (w);
        g.setOpacity (bright);
        put (g, p, cx - d.box, cy - d.box);
    };
    int run = -1;
    for (int k = 0; k <= 16; ++k)
    {
        const float lo = (float) k / 16.0f, hi = (float) (k + 1) / 16.0f;
        const float part = k < 16 ? juce::jlimit (0.0f, 1.0f, (std::min (b, hi) - std::max (a, lo)) * 16.0f) : 0.0f;
        if (part > 0.995f) { if (run < 0) run = k; continue; }
        if (run >= 0) { wedge ((float) run / 16.0f, lo, 1.0f); run = -1; }
        if (part > 0.02f) wedge (lo, hi, 0.25f + 0.75f * part);
    }
}

void Skin::key (G& g, R r, Colour c, bool down)
{
    const int w = juce::roundToInt (r.getWidth()), h = juce::roundToInt (r.getHeight());
    const auto& p = pic (Key { wKey, w, h, c.getARGB(), down ? 1 : 0 }, [&]
    {
        Relief rf ((float) w, (float) h, scale, 8.0f, 2);
        const R b (0.0f, 0.0f, (float) w, (float) h);
        const bool on = emits (c);
        rf.slab (b, 2.6f, -3.0f, 0.0f, {}, true);                                                  // the hole it moves in
        rf.slab (b.reduced (0.7f), 2.2f, (down ? 0.4f : 1.27f) * kU, 1.4f, { on ? c.withMultipliedSaturation (1.1f).withMultipliedBrightness (1.25f) : c, 0.32f, 0.03f, on ? 0.45f : 0.0f });
        return from (rf);
    });
    g.setOpacity (1.0f);
    put (g, p, r.getX(), r.getY());
}

void Skin::lamp (G& g, float cx, float cy, Colour c, float d, bool on)
{
    const int di = juce::roundToInt (d);
    const auto& p = pic (Key { wLamp, di, 0, c.getARGB(), on ? 1 : 0 }, [&]
    {
        Relief rf (d, d, scale, 6.0f, 3);
        rf.disc (d * 0.5f, d * 0.5f, d * 0.5f, 0.7f, 0.5f, { Colour (0xff1E1F1D), 0.5f });
        rf.disc (d * 0.5f, d * 0.5f, d * 0.5f - 2.0f, 2.6f, 2.2f, { on ? led (c) : dark ? kNightLens : Colour (0xff6C706A), 0.75f, 0.0f, on ? 1.0f : 0.0f });
        return from (rf);
    });
    g.setOpacity (1.0f);
    put (g, p, cx - d * 0.5f, cy - d * 0.5f);
}

void Skin::hollow (G& g, float cx, float cy, float d, bool round)
{
    const int di = juce::roundToInt (d);
    const auto& p = pic (Key { wHollow, di, 0, 0, round ? 1 : 0 }, [&]
    {
        Relief rf (d, d, scale, 4.0f, 2);
        if (round) rf.disc (d * 0.5f, d * 0.5f, d * 0.5f - 1.0f, -1.5f, 0.0f, {}, true);
        else rf.slab (R (1.0f, 1.0f, d - 2.0f, d - 2.0f), 2.6f, -1.5f, 0.0f, {}, true);
        return from (rf);
    });
    g.setOpacity (1.0f);
    put (g, p, cx - d * 0.5f, cy - d * 0.5f);
    if (dark)
    {
        // (shade does not show on a dark plate: a faint line keeps the place findable)
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        if (round) g.drawEllipse (cx - d * 0.5f + 1.5f, cy - d * 0.5f + 1.5f, d - 3.0f, d - 3.0f, 1.0f);
        else g.drawRoundedRectangle (R (cx - d * 0.5f + 1.5f, cy - d * 0.5f + 1.5f, d - 3.0f, d - 3.0f), 2.2f, 1.0f);
    }
}

// A display: the surface is cut down to a pane. The cut's upper and left walls are in shade and
// shade the pane; its lower and right edges catch the light.
void Skin::window (G& g, R r, Colour glass)
{
    const bool deep = glass.getPerceivedBrightness() < 0.4f;        // a dark pane
    g.setColour (juce::Colours::black.withAlpha (dark ? 0.60f : 0.45f));
    g.fillRoundedRectangle (r.expanded (0.8f), 2.2f);
    g.setColour (juce::Colours::white.withAlpha (dark ? 0.16f : 0.50f));       // (a dark plate's edge catches less)
    g.fillRect (R (r.getX() + 1.5f, r.getBottom() + 0.3f, r.getWidth() - 2.0f, 0.9f));
    g.fillRect (R (r.getRight() + 0.3f, r.getY() + 1.5f, 0.9f, r.getHeight() - 2.0f));
    g.setColour (deep ? glass.darker (0.35f) : glass.getPerceivedBrightness() > 0.8f ? glass.interpolatedWith (Colour (0xffD3D6CB), 0.35f) : glass);      // (a coloured pane keeps its colour)
    g.fillRoundedRectangle (r, 1.4f);
    const float top = std::min (6.0f, r.getHeight() * 0.3f), left = std::min (4.0f, r.getWidth() * 0.2f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (deep ? 0.55f : 0.30f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getY() + top, false));
    g.fillRect (r.withHeight (top));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (deep ? 0.35f : 0.18f), r.getX(), 0.0f, juce::Colours::transparentBlack, r.getX() + left, 0.0f, false));
    g.fillRect (r.withWidth (left));
    if (deep)
    {
        // the pane's shine
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), r.getX(), r.getBottom(), juce::Colours::transparentWhite, r.getX() + r.getHeight() * 0.8f, r.getY(), false));
        g.fillRect (r.withTrimmedTop (r.getHeight() * 0.45f));
    }
}

void Skin::badge (G& g, R r, const juce::String& name, const juce::Font& font)
{
    const int w = juce::roundToInt (r.getWidth()), h = juce::roundToInt (r.getHeight());
    const auto& p = pic (Key { wBadge, w, h, 0, 0 }, [&]
    {
        const int fineBy = 2;
        Relief rf ((float) w, (float) h, scale, 8.0f, fineBy);
        const R b (0.0f, 0.0f, (float) w, (float) h);
        rf.slab (b, 5.0f, 1.27f * kU, 1.2f, { dark ? kNightBadge : kBlack, dark ? 0.6f : 0.35f, 0.05f });
        const float ms = scale * (float) fineBy;
        juce::Image mask (juce::Image::ARGB, (int) std::ceil ((float) w * ms), (int) std::ceil ((float) h * ms), true);
        {
            G mg (mask);
            mg.addTransform (juce::AffineTransform::scale (ms));
            mg.setColour (juce::Colours::white); mg.setFont (font);
            mg.drawText (name, b, juce::Justification::centred, false);
        }
        rf.letters (mask, R (0.0f, 0.0f, (float) mask.getWidth() / ms, (float) mask.getHeight() / ms), 1.52f * kU, { Colour (0xffF6F6F6), 0.4f });
        return from (rf);
    });
    g.setOpacity (1.0f);
    put (g, p, r.getX(), r.getY());
}
} // namespace sf0
