#include "Stage.h"
#include "Engine.h"
#include <juce_opengl/juce_opengl.h>
#include <atomic>
#include <cmath>
#include <cstring>

// The 3D stage's drawing. Everything here is OpenGL 3.2 core (GLSL 1.50), the oldest that macOS
// still gives a core profile for, with nothing in it that needs more.
namespace sf0
{
using namespace juce::gl;

namespace
{
// ------------------------------------------------------------------------------------- numbers
struct V3
{
    float x = 0, y = 0, z = 0;
    V3 operator+ (V3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator- (V3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator* (float s) const { return { x * s, y * s, z * s }; }
    float dot (V3 o) const { return x * o.x + y * o.y + z * o.z; }
    V3 cross (V3 o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    float length() const { return std::sqrt (dot (*this)); }
    V3 unit() const { const float l = length(); return l > 1.0e-9f ? *this * (1.0f / l) : V3 { 0, 1, 0 }; }
    float distance (V3 o) const { return (*this - o).length(); }
    V3 toward (V3 o, float k) const { return *this + (o - *this) * k; }
};

struct M4
{
    float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };      // columns, one after another
    M4 operator* (const M4& b) const
    {
        M4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    // a point through it, then divided through: where it lands in the window's own -1..1 (w: how far in front)
    V3 point (V3 p, float* w = nullptr) const
    {
        const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14], ww = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (w != nullptr) *w = ww;
        const float inv = std::abs (ww) > 1.0e-9f ? 1.0f / ww : 0.0f;
        return { x * inv, y * inv, z * inv };
    }
    static M4 perspective (float fovy, float aspect, float zn, float zf)
    {
        M4 r; std::memset (r.m, 0, sizeof (r.m));
        const float f = 1.0f / std::tan (fovy * 0.5f);
        r.m[0] = f / aspect; r.m[5] = f; r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1.0f; r.m[14] = 2.0f * zf * zn / (zn - zf);
        return r;
    }
    static M4 ortho (float l, float r, float b, float t, float zn, float zf)
    {
        M4 o;
        o.m[0] = 2.0f / (r - l); o.m[5] = 2.0f / (t - b); o.m[10] = -2.0f / (zf - zn);
        o.m[12] = -(r + l) / (r - l); o.m[13] = -(t + b) / (t - b); o.m[14] = -(zf + zn) / (zf - zn);
        return o;
    }
    static M4 look (V3 eye, V3 at, V3 up)
    {
        const V3 f = (at - eye).unit(), s = f.cross (up).unit(), u = s.cross (f);
        M4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;   r.m[12] = -s.dot (eye);
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;   r.m[13] = -u.dot (eye);
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = f.dot (eye);
        return r;
    }
    bool inverse (M4& out) const
    {
        double inv[16], a[16];
        for (int i = 0; i < 16; ++i) a[i] = m[i];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (std::abs (det) < 1.0e-30) return false;
        det = 1.0 / det;
        for (int i = 0; i < 16; ++i) out.m[i] = (float) (inv[i] * det);
        return true;
    }
};

constexpr float kPi = 3.14159265f;
constexpr float kFov = 22.0f * kPi / 180.0f;        // a long lens: little keystone, the print stays readable
inline float rad (float deg) { return deg * kPi / 180.0f; }
inline float lin1 (float c) { return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); }
struct Rgb { float r = 0, g = 0, b = 0; Rgb operator* (float s) const { return { r * s, g * s, b * s }; } };
inline Rgb lin (juce::Colour c) { return { lin1 (c.getFloatRed()), lin1 (c.getFloatGreen()), lin1 (c.getFloatBlue()) }; }
inline Rgb lin (juce::uint32 argb) { return lin (juce::Colour (argb)); }

// The eye's side of the instrument, from degrees round and up.
inline V3 sideOf (float turn, float tilt)
{
    const float a = rad (turn), e = rad (juce::jlimit (4.0f, 89.9f, tilt));
    return { std::sin (a) * std::cos (e), std::sin (e), std::cos (a) * std::cos (e) };
}

// Where the eye stands to hold these corners in the window, looking from that side: what it looks
// at, and from how far. (Twelve rounds of: stand there, see where the corners fall, step back or in
// by as much as they overflow, and look at their middle.)
void stand (V3 side, const V3* corners, int n, float aspect, V3& at, float& away)
{
    const float left = -0.94f, right = 0.94f, top = 0.92f, bottom = -0.92f;
    at = {};
    for (int i = 0; i < n; ++i) at = at + corners[i] * (1.0f / (float) n);
    float d = 4000.0f;
    const M4 proj = M4::perspective (kFov, aspect, 10.0f, 60000.0f);
    for (int pass = 0; pass < 12; ++pass)
    {
        const V3 eye = at + side * d;
        const M4 view = M4::look (eye, at, { 0, 1, 0 }), vp = proj * view;
        float x0 = 9, x1 = -9, y0 = 9, y1 = -9;
        for (int i = 0; i < n; ++i) { const V3 p = vp.point (corners[i]); x0 = std::min (x0, p.x); x1 = std::max (x1, p.x); y0 = std::min (y0, p.y); y1 = std::max (y1, p.y); }
        const float k = std::max ((x1 - x0) / (right - left), (y1 - y0) / (top - bottom));
        const float ex = (x0 + x1) * 0.5f - (left + right) * 0.5f, ey = (y0 + y1) * 0.5f - (top + bottom) * 0.5f, half = std::tan (kFov * 0.5f) * d;
        const V3 f = (at - eye).unit(), s = f.cross ({ 0, 1, 0 }).unit(), u = s.cross (f);
        at = at + s * (ex * half * aspect) + u * (ey * half);
        d = juce::jlimit (260.0f, 30000.0f, d * k);
    }
    away = d;
}

// ------------------------------------------------------------------------------------- shapes
// A slab: a box with rounded corners and a rounded top edge, of any size. One mesh serves them
// all: each vertex says which corner it belongs to, which way out from that corner's centre it
// lies, and where on the wall, the roll of the edge or the top it is; the size comes with each
// slab drawn, so the corners stay round whatever the size.
struct Mesh { std::vector<float> v; std::vector<juce::uint32> i; int stride = 0; };

Mesh slabMesh()
{
    Mesh m; m.stride = 10;
    const int S = 6, B = 4, N = 4 * (S + 1);
    auto ring = [&] (float inset, float wall, float roll, float nr, float ny, float topFlag)
    {
        for (int c = 0; c < 4; ++c)
            for (int j = 0; j <= S; ++j)
            {
                const float th = ((float) c + (float) j / (float) S) * kPi * 0.5f, ax = std::cos (th), az = std::sin (th);
                const float sx = c == 0 || c == 3 ? 1.0f : -1.0f, sz = c < 2 ? 1.0f : -1.0f;
                for (float f : { sx, sz, ax, az, inset, wall, roll, nr, ny, topFlag }) m.v.push_back (f);
            }
    };
    ring (0, 0, 0, 1, 0, 0);            // the foot of the wall
    ring (0, 1, 0, 1, 0, 0);            // its top, where the edge starts to roll
    for (int j = 1; j <= B; ++j) { const float th = (float) j / (float) B * kPi * 0.5f; ring (1.0f - std::cos (th), 1, std::sin (th), std::cos (th), std::sin (th), 0); }
    ring (1, 1, 1, 0, 1, 1);            // the rim of the flat top
    const int rings = 3 + B;
    for (float f : { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f }) m.v.push_back (f);      // the middle of the top
    for (int r = 0; r + 1 < rings; ++r)
        for (int k = 0; k < N; ++k)
        {
            const juce::uint32 a = (juce::uint32) (r * N + k), b = (juce::uint32) (r * N + (k + 1) % N), c = a + (juce::uint32) N, d = b + (juce::uint32) N;
            for (juce::uint32 x : { a, b, d, a, d, c }) m.i.push_back (x);
        }
    const juce::uint32 mid = (juce::uint32) (rings * N), base = (juce::uint32) ((rings - 1) * N);
    for (int k = 0; k < N; ++k) for (juce::uint32 x : { base + (juce::uint32) k, base + (juce::uint32) ((k + 1) % N), mid }) m.i.push_back (x);
    return m;
}

// A turned shape (a cap, a lens): an outline of (radius, height) spun round. Where the outline
// turns a sharp corner the surface does too; elsewhere it is smooth.
Mesh turned (std::vector<std::pair<float, float>> outline, int segs)
{
    Mesh m; m.stride = 7;
    struct P { float r, y, nr, ny; };
    std::vector<P> pts;
    const int n = (int) outline.size();
    auto normalOf = [&] (int a, int b) { const float dr = outline[(size_t) b].first - outline[(size_t) a].first, dy = outline[(size_t) b].second - outline[(size_t) a].second, l = std::sqrt (dr * dr + dy * dy) + 1.0e-9f; return std::pair<float, float> (dy / l, -dr / l); };
    for (int i = 0; i < n; ++i)
    {
        const auto [r, y] = outline[(size_t) i];
        if (i == 0) { const auto nn = normalOf (0, 1); pts.push_back ({ r, y, nn.first, nn.second }); continue; }
        if (i == n - 1) { const auto nn = normalOf (n - 2, n - 1); pts.push_back ({ r, y, nn.first, nn.second }); continue; }
        const auto a = normalOf (i - 1, i), b = normalOf (i, i + 1);
        if (a.first * b.first + a.second * b.second < 0.72f) { pts.push_back ({ r, y, a.first, a.second }); pts.push_back ({ r, y, b.first, b.second }); }
        else { const float sr = a.first + b.first, sy = a.second + b.second, l = std::sqrt (sr * sr + sy * sy) + 1.0e-9f; pts.push_back ({ r, y, sr / l, sy / l }); }
    }
    const float topY = outline.back().second;
    const int cols = segs + 1, rows = (int) pts.size();
    for (int j = 0; j < rows; ++j)
        for (int k = 0; k < cols; ++k)
        {
            const float th = (float) k / (float) segs * 2.0f * kPi, cs = std::cos (th), sn = std::sin (th);
            const auto& p = pts[(size_t) j];
            const float flat = p.ny > 0.999f && std::abs (p.y - topY) < 1.0e-4f ? 1.0f : 0.0f;
            for (float f : { p.r * cs, p.y, p.r * sn, p.nr * cs, p.ny, p.nr * sn, flat }) m.v.push_back (f);
        }
    for (int j = 0; j + 1 < rows; ++j)
    {
        if (pts[(size_t) j].r == pts[(size_t) j + 1].r && pts[(size_t) j].y == pts[(size_t) j + 1].y) continue;       // (the two sides of a corner)
        for (int k = 0; k < segs; ++k)
        {
            const juce::uint32 a = (juce::uint32) (j * cols + k), b = a + 1, c = a + (juce::uint32) cols, d = c + 1;
            for (juce::uint32 x : { a, b, d, a, d, c }) m.i.push_back (x);
        }
    }
    return m;
}

// One light of a knob's ring: a short curved block lying toward the player (six o'clock).
Mesh segmentMesh (float r0, float r1, float y0, float y1)
{
    Mesh m; m.stride = 3;
    const float half = rad (16.64f) * 0.5f;
    const int n = 3;
    for (int i = 0; i <= n; ++i)
    {
        const float t = -half + 2.0f * half * (float) i / (float) n, s = std::sin (t), c = std::cos (t);
        for (float f : { s * r0, y0, c * r0, s * r1, y0, c * r1, s * r0, y1, c * r0, s * r1, y1, c * r1 }) m.v.push_back (f);
    }
    for (int i = 0; i < n; ++i)
    {
        const juce::uint32 o = (juce::uint32) (i * 4);
        for (juce::uint32 x : { o + 2, o + 3, o + 7, o + 2, o + 7, o + 6, o + 1, o + 5, o + 7, o + 1, o + 7, o + 3, o, o + 2, o + 6, o, o + 6, o + 4 }) m.i.push_back (x);
    }
    const juce::uint32 e = (juce::uint32) (n * 4);
    for (juce::uint32 x : { 0u, 1u, 3u, 0u, 3u, 2u, e, e + 2, e + 3, e, e + 3, e + 1 }) m.i.push_back (x);
    return m;
}

// ------------------------------------------------------------------------------------ shaders
const char* const kSlabVert = R"(#version 150
in vec2 aCorner; in vec2 aArc; in vec3 aProf; in vec2 aN; in float aTop;
in vec4 iRect; in vec4 iShape; in vec4 iColour; in vec4 iMat; in vec4 iClip;
uniform mat4 uVP;
out vec3 vPos; out vec3 vNormal; out float vTop; out vec4 vColour; out vec4 vMat; out vec4 vClip; flat out vec4 vRect;
void main()
{
    vRect = iRect;
    vec2 hs = iRect.zw;
    float r = min (iShape.z, min (hs.x, hs.y));
    float bev = min (iShape.w, r);
    vec2 xz = iRect.xy + aCorner * (hs - vec2 (r)) + aArc * (r - bev * aProf.x);
    float y = iShape.x + (iShape.y - bev) * aProf.y + bev * aProf.z;
    vPos = vec3 (xz.x, y, xz.y);
    vNormal = vec3 (aArc.x * aN.x, aN.y, aArc.y * aN.x);
    vTop = aTop; vColour = iColour; vMat = iMat; vClip = iClip;
    gl_Position = uVP * vec4 (vPos, 1.0);
}
)";

const char* const kTurnedVert = R"(#version 150
in vec3 aPos; in vec3 aNormal; in float aTop;
in vec4 iPos; in vec4 iColour; in vec4 iMat; in vec4 iClip;
uniform mat4 uVP;
out vec3 vPos; out vec3 vNormal; out float vTop; out vec4 vColour; out vec4 vMat; out vec4 vClip; flat out vec4 vRect;
void main()
{
    vRect = vec4 (0.0);
    vPos = vec3 (iPos.x + aPos.x * iPos.w, iPos.z + aPos.y * iPos.w, iPos.y + aPos.z * iPos.w);
    vNormal = aNormal;
    vTop = aTop; vColour = iColour; vMat = iMat; vClip = iClip;
    gl_Position = uVP * vec4 (vPos, 1.0);
}
)";

// A light: it is its colour, whatever falls on it. (iColour.a: how big; iPos.w: turned how far round.)
const char* const kLightVert = R"(#version 150
in vec3 aPos;
in vec4 iPos; in vec4 iColour; in vec4 iClip;
uniform mat4 uVP;
out vec3 vPos; out vec3 vColour; out vec4 vClip;
void main()
{
    float c = cos (iPos.w), s = sin (iPos.w);
    vec3 p = vec3 (aPos.x * c + aPos.z * s, aPos.y, aPos.z * c - aPos.x * s) * iColour.a;
    vPos = vec3 (iPos.x + p.x, iPos.z + p.y, iPos.y + p.z);
    vColour = iColour.rgb; vClip = iClip;
    gl_Position = uVP * vec4 (vPos, 1.0);
}
)";

const char* const kLightFrag = R"(#version 150
in vec3 vPos; in vec3 vColour; in vec4 vClip;
out vec4 oColour;
void main()
{
    if (vPos.x < vClip.x || vPos.z < vClip.y || vPos.x > vClip.z || vPos.z > vClip.w) discard;
    oColour = vec4 (clamp (vColour, 0.0, 60.0), 1.0);
}
)";

// What casts a shadow is drawn from the light's side with only its depth kept.
const char* const kDepthFrag = R"(#version 150
in vec3 vPos; in vec4 vClip;
out vec4 oColour;
void main()
{
    if (vPos.x < vClip.x || vPos.z < vClip.y || vPos.x > vClip.z || vPos.z > vClip.w) discard;
    oColour = vec4 (1.0);
}
)";

// Every solid surface. vMat.x says what it is: 0 plain; 1 its top carries the sheet's print;
// 2 a display (what the sheet shows there is light); 3 a lit key; 4 the case.
const char* const kSurfaceFrag = R"(#version 150
in vec3 vPos; in vec3 vNormal; in float vTop; in vec4 vColour; in vec4 vMat; in vec4 vClip; flat in vec4 vRect;
uniform vec3 uEye; uniform vec3 uSun; uniform vec3 uSunCol; uniform vec3 uSky; uniform vec3 uGround; uniform float uFinish;
uniform sampler2D uSheet; uniform vec4 uSheetMap;
uniform sampler2DShadow uShadow; uniform mat4 uShadowVP; uniform float uShadowTexel;
uniform vec3 uWarm; uniform float uCaseDepth;
out vec4 oColour;
const float PI = 3.14159265;
const vec2 kTaps[9] = vec2[9] (vec2 (0.0, 0.0), vec2 (0.95, 0.20), vec2 (-0.35, 0.90), vec2 (-0.90, -0.30), vec2 (0.30, -0.95),
                               vec2 (0.55, 0.65), vec2 (-0.70, 0.45), vec2 (-0.50, -0.75), vec2 (0.75, -0.50));
vec3 toLinear (vec3 c) { return mix (c / 12.92, pow ((c + vec3 (0.055)) / 1.055, vec3 (2.4)), step (vec3 (0.04045), c)); }
// numbers with no pattern to them, from a place: one for a point, and one that changes smoothly between points
float hash1 (vec2 p) { vec3 q = fract (vec3 (p.xyx) * 0.1031); q += dot (q, q.yzx + 33.33); return fract ((q.x + q.y) * q.z); }
float smoothNoise (vec2 p)
{
    vec2 i = floor (p), f = fract (p);
    f = f * f * (3.0 - 2.0 * f);
    return mix (mix (hash1 (i), hash1 (i + vec2 (1.0, 0.0)), f.x), mix (hash1 (i + vec2 (0.0, 1.0)), hash1 (i + vec2 (1.0, 1.0)), f.x), f.y);
}
float shade (vec3 p, vec3 n)
{
    vec4 q = uShadowVP * vec4 (p + n * 0.7, 1.0);
    vec3 c = q.xyz * 0.5 + vec3 (0.5);
    if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;
    float s = 0.0;
    for (int i = 0; i < 9; ++i) s += texture (uShadow, vec3 (c.xy + kTaps[i] * uShadowTexel * 1.7, c.z - 0.0005));
    return s / 9.0;
}
void main()
{
    if (vPos.x < vClip.x || vPos.z < vClip.y || vPos.x > vClip.z || vPos.z > vClip.w) discard;
    vec3 n = normalize (vNormal);
    if (! gl_FrontFacing) n = -n;
    vec3 albedo = vColour.rgb;
    float rough = clamp (vColour.a, 0.06, 1.0), metal = clamp (vMat.z, 0.0, 1.0), kind = vMat.x;
    vec3 emit = vec3 (0.0);
    if (vTop > 0.5 && vMat.w > 0.5 && uFinish > 0.0)
    {
        // A faceplate: anodized aluminium that has been used. Its dye is never quite even (a slow
        // mottling, a fine blasted grain); along its edges, in patches, hands and cases have rubbed
        // it toward the metal; and here and there something has been dragged across it. All of it slight.
        vec2 p = vPos.xz;
        float grain = hash1 (floor (p * 2.3)) - 0.5;
        float mottle = smoothNoise (p * 0.035) * 0.6 + smoothNoise (p * 0.11 + vec2 (17.0, 5.0)) * 0.4 - 0.5;
        float edge = min (vRect.z - abs (p.x - vRect.x), vRect.w - abs (p.y - vRect.y));
        float worn = (1.0 - smoothstep (0.6, 7.0, edge)) * smoothstep (0.42, 0.80, smoothNoise (p * 0.06 + vec2 (3.0, 41.0)));
        vec2 along = vec2 (0.83, 0.56), across = vec2 (-0.56, 0.83);
        float scuff = smoothstep (0.94, 1.0, smoothNoise (vec2 (dot (p, across) * 2.6, dot (p, along) * 0.020)))
                    + smoothstep (0.96, 1.0, smoothNoise (vec2 (dot (p, along) * 2.1 + 9.0, dot (p, across) * 0.014 + 4.0)));
        scuff *= smoothstep (0.35, 0.75, smoothNoise (p * 0.02 + vec2 (60.0, 12.0)));
        float f = uFinish;
        albedo *= 1.0 + f * (grain * 0.06 + mottle * 0.07);
        albedo = mix (albedo, vec3 (0.42, 0.43, 0.44), f * (worn * 0.24 + scuff * 0.04));
        rough = clamp (rough + f * (grain * 0.05 - mottle * 0.06 - worn * 0.18 - scuff * 0.08), 0.06, 1.0);
        metal = clamp (metal + f * worn * 0.45, 0.0, 1.0);
    }
    if (vTop > 0.5 && kind > 0.5)
    {
        vec4 p = texture (uSheet, (vPos.xz + uSheetMap.xy) * uSheetMap.zw);
        float cover = clamp (p.a, 0.0, 1.0);
        vec3 ink = cover > 0.002 ? toLinear (clamp (p.rgb / cover, 0.0, 1.0)) : vec3 (0.0);
        albedo = mix (albedo, ink, cover);
        if (kind > 1.5 && kind < 2.5)
        {
            // a display: what it shows is light. A dark one's letters glow; a pale one is lit evenly and does not dazzle.
            float pale = smoothstep (0.15, 0.55, dot (vColour.rgb, vec3 (0.2126, 0.7152, 0.0722)));
            emit = albedo * mix (1.35, 0.80, pale);
            albedo *= 0.10;
        }
    }
    if (kind > 2.5 && kind < 3.5) emit += vColour.rgb * vMat.y;
    if (kind > 3.5)
    {
        float drop = clamp (-vPos.y / uCaseDepth, 0.0, 1.0);
        emit += uWarm * (drop * drop * sqrt (drop)) * clamp (1.0 - n.y, 0.0, 1.0);
    }
    vec3 V = normalize (uEye - vPos);
    float ndl = max (dot (n, uSun), 0.0), ndv = max (dot (n, V), 0.001);
    float sh = ndl > 0.0 ? shade (vPos, n) : 0.0;
    vec3 f0 = mix (vec3 (0.04), albedo, metal);
    vec3 diff = albedo * (1.0 - metal);
    vec3 H = normalize (uSun + V);
    float ndh = max (dot (n, H), 0.0), vdh = max (dot (V, H), 0.0);
    float a = rough * rough, a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * d * d);
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));
    vec3 F = f0 + (vec3 (1.0) - f0) * pow (1.0 - vdh, 5.0);
    vec3 c = (diff / PI + D * G * F / max (4.0 * ndl * ndv, 0.001)) * uSunCol * ndl * sh;
    c += diff * mix (uGround, uSky, n.y * 0.5 + 0.5);
    // what shines in it: a soft room, brighter overhead
    vec3 Rv = reflect (-V, n);
    vec3 room = mix (uGround, uSky, smoothstep (-0.3, 0.8, Rv.y)) * 1.5;
    vec3 Fr = f0 + (max (vec3 (1.0 - rough), f0) - f0) * pow (1.0 - ndv, 5.0);
    c += room * Fr * (1.0 - rough * 0.7);
    c += emit;
    // (nothing leaves here that is not a number: one that is not would be spread over the whole picture by the glow)
    if (! (c.r >= 0.0)) c.r = 0.0;
    if (! (c.g >= 0.0)) c.g = 0.0;
    if (! (c.b >= 0.0)) c.b = 0.0;
    oColour = vec4 (min (c, vec3 (60.0)), 1.0);
}
)";

// A window-filling pair of triangles, for the passes that work on whole pictures.
const char* const kFullVert = R"(#version 150
in vec2 aPos;
out vec2 vUv;
void main() { vUv = aPos; gl_Position = vec4 (aPos * 2.0 - 1.0, 0.0, 1.0); }
)";

// The room behind the instrument: dark, a little lit behind its middle.
const char* const kRoomFrag = R"(#version 150
in vec2 vUv;
uniform float uAspect; uniform vec3 uNear; uniform vec3 uFar;
out vec4 oColour;
void main()
{
    vec2 p = (vUv - vec2 (0.5, 0.56)) * vec2 (uAspect, 1.0);
    float d = clamp (length (p) / 0.95, 0.0, 1.0);
    oColour = vec4 (mix (uNear, uFar, smoothstep (0.0, 1.0, d)), 1.0);
}
)";

// The glow under the case: the case's own outline, soft, on a sheet below it.
const char* const kGlowVert = R"(#version 150
in vec2 aPos;
uniform mat4 uVP; uniform vec4 uPlane; uniform float uY;
out vec2 vAt;
void main() { vAt = uPlane.xy + (aPos * 2.0 - 1.0) * uPlane.zw; gl_Position = uVP * vec4 (vAt.x, uY, vAt.y, 1.0); }
)";
const char* const kGlowFrag = R"(#version 150
in vec2 vAt;
uniform vec4 uBox; uniform vec3 uGlow; uniform float uSoft;
out vec4 oColour;
void main()
{
    vec2 q = abs (vAt - uBox.xy) - uBox.zw;
    float sd = length (max (q, vec2 (0.0))) + min (max (q.x, q.y), 0.0);
    float g = exp (-pow (max (sd + uSoft * 0.25, 0.0) / uSoft, 2.0));
    oColour = vec4 (uGlow * g, 1.0);
}
)";

// The glow round the lights: the bright parts of the picture, made smaller and softer step by
// step, then brought back up and laid over it.
const char* const kDownFrag = R"(#version 150
in vec2 vUv;
uniform sampler2D uTex; uniform vec2 uTexel; uniform float uKnee;
out vec4 oColour;
vec3 at (float x, float y) { return texture (uTex, vUv + vec2 (x, y) * uTexel).rgb; }
void main()
{
    vec3 e = at (0.0, 0.0);
    vec3 c = e * 0.125 + (at (-2.0, 2.0) + at (2.0, 2.0) + at (-2.0, -2.0) + at (2.0, -2.0)) * 0.03125
           + (at (0.0, 2.0) + at (-2.0, 0.0) + at (2.0, 0.0) + at (0.0, -2.0)) * 0.0625
           + (at (-1.0, 1.0) + at (1.0, 1.0) + at (-1.0, -1.0) + at (1.0, -1.0)) * 0.125;
    if (uKnee > 0.0)
    {
        float peak = max (c.r, max (c.g, c.b));
        c *= smoothstep (uKnee, uKnee * 1.5, peak);
    }
    if (! (c.r >= 0.0)) c.r = 0.0;
    if (! (c.g >= 0.0)) c.g = 0.0;
    if (! (c.b >= 0.0)) c.b = 0.0;
    oColour = vec4 (min (c, vec3 (60.0)), 1.0);
}
)";
const char* const kUpFrag = R"(#version 150
in vec2 vUv;
uniform sampler2D uTex; uniform vec2 uTexel; uniform float uGain;
out vec4 oColour;
vec3 at (float x, float y) { return texture (uTex, vUv + vec2 (x, y) * uTexel).rgb; }
void main()
{
    vec3 c = at (0.0, 0.0) * 4.0 + (at (-1.0, 0.0) + at (1.0, 0.0) + at (0.0, -1.0) + at (0.0, 1.0)) * 2.0
           + at (-1.0, -1.0) + at (1.0, -1.0) + at (-1.0, 1.0) + at (1.0, 1.0);
    oColour = vec4 (c * (uGain / 16.0), 1.0);
}
)";

// The picture as the window shows it: the glow laid on, the brightest parts eased down, and the
// whole set in the display's own scale of greys, with a grain too fine to see that stops the dark
// gradients from banding.
const char* const kFinalFrag = R"(#version 150
in vec2 vUv;
uniform sampler2D uTex; uniform sampler2D uBloom; uniform float uBloomGain; uniform float uExposure;
out vec4 oColour;
vec3 ease (vec3 c)
{
    float x = min (c.r, min (c.g, c.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    c -= vec3 (offset);
    float peak = max (c.r, max (c.g, c.b));
    if (peak < 0.76) return c;
    float d = 0.24;
    float newPeak = 1.0 - d * d / (peak + d - 0.76);
    c *= newPeak / peak;
    float g = 1.0 - 1.0 / (0.15 * (peak - newPeak) + 1.0);
    return mix (c, vec3 (newPeak), g);
}
void main()
{
    vec3 c = texture (uTex, vUv).rgb + texture (uBloom, vUv).rgb * uBloomGain;
    c = ease (max (c, vec3 (0.0)) * uExposure);
    c = clamp (c, 0.0, 1.0);
    c = mix (c * 12.92, 1.055 * pow (c, vec3 (1.0 / 2.4)) - vec3 (0.055), step (vec3 (0.0031308), c));
    float grain = fract (sin (dot (gl_FragCoord.xy, vec2 (12.9898, 78.233))) * 43758.5453) - 0.5;
    oColour = vec4 (c + vec3 (grain / 255.0), 1.0);
}
)";

const char* const kCopyFrag = R"(#version 150
in vec2 vUv;
uniform sampler2D uTex;
out vec4 oColour;
void main() { oColour = vec4 (texture (uTex, vUv).rgb, 1.0); }
)";

// A tag: a picture, where it is put on the window.
const char* const kTagVert = R"(#version 150
in vec2 aPos;
uniform vec4 uRect;
out vec2 vUv;
void main() { vUv = vec2 (aPos.x, 1.0 - aPos.y); gl_Position = vec4 (uRect.xy + aPos * uRect.zw, 0.0, 1.0); }
)";
const char* const kTagFrag = R"(#version 150
in vec2 vUv;
uniform sampler2D uTex; uniform float uAlpha;
out vec4 oColour;
void main() { oColour = texture (uTex, vUv) * uAlpha; }
)";

// The knob caps, as the model has them (radius, height above the plate; screen units).
const std::vector<std::pair<float, float>> kCapBig { { 0.0f, 0.0f }, { 11.12f, 0.0f }, { 11.84f, 0.16f }, { 11.12f, 8.30f }, { 11.12f, 16.65f }, { 11.84f, 24.84f }, { 11.10f, 25.28f }, { 10.75f, 26.37f },
                                                     { 10.20f, 27.03f }, { 9.47f, 27.52f }, { 8.63f, 27.74f }, { 0.0f, 27.74f } };
const std::vector<std::pair<float, float>> kCapSmall { { 0.0f, 0.0f }, { 7.78f, 0.0f }, { 8.29f, 0.49f }, { 7.78f, 6.22f }, { 7.78f, 12.07f }, { 8.29f, 17.80f }, { 7.77f, 18.07f }, { 7.61f, 18.67f },
                                                       { 7.14f, 19.33f }, { 6.25f, 19.82f }, { 0.0f, 19.82f } };
} // namespace

// =========================================================================================== Impl
struct Stage::Impl : public juce::OpenGLRenderer, private juce::AsyncUpdater
{
    explicit Impl (Stage& s, Engine& e) : owner (s), eng (e)
    {
        // The sheet's fineness: three pixels to the unit on a display with two pixels to the point
        // (the print then stays clean when the eye is close to one module), two on a plainer one.
        // SF000_SHEET sets it by hand.
        float dense = 1.0f;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) dense = (float) d->scale;
        const float sc = juce::jlimit (1.0f, 4.0f, juce::SystemStats::getEnvironmentVariable ("SF000_SHEET", dense >= 1.5f ? "3" : "2").getFloatValue());
        sheetS = sc;
        sheetW = ((int) std::ceil (kW * sc) + kTile - 1) / kTile * kTile;
        sheetH = ((int) std::ceil (kH * sc) + kTile - 1) / kTile * kTile;
        sheetImg = juce::Image (juce::Image::ARGB, sheetW, sheetH, true, juce::SoftwareImageType());
        sentPx.calloc ((size_t) sheetW * (size_t) sheetH * 4);
        homeEye = { 0, 3000, 2000 }; curEye = goalEye = softEye = homeEye;
    }
    ~Impl() override { cancelPendingUpdate(); }

    Stage& owner;
    Engine& eng;
    juce::OpenGLContext context;
    juce::Component* comp = nullptr;

    // ---- between the threads
    juce::CriticalSection lock;
    struct SheetUpdate { int level = 0; juce::Rectangle<int> r; juce::HeapBlock<juce::uint8> px; };
    struct Pending
    {
        bool hasParts = false; Parts parts;
        std::vector<SheetUpdate> sheet; size_t sheetBytes = 0;
        bool hasTags = false; std::vector<Tag> tags;
        bool dark = false;
        // the eye: where home is, where it is to go, and how
        V3 homeEye, homeAt, goalEye, goalAt; bool jump = true, eyeSet = false;
        bool capture = false, snap = false;
    } pending;
    std::atomic<int> stateNow { (int) State::starting };
    juce::String why, driverName;
    std::atomic<bool> away { false }, gotFrame { false }, sheetOverflow { false };
    std::atomic<int> frameCount { 0 }, buildCount { 0 };
    std::atomic<float> glowHue { Stage::kGlowHue }, glowGain { 1.0f }, glowPulse { 0.0f };
    // the kick, as the drawing thread follows it for the glow: the loudest its meter has lately been, and how near it is to that now
    float kickTop = -100.0f, kickEnv = 0.0f, kickShown = 0.0f;
    float glowNow() const { const float p = glowPulse.load(); return glowGain.load() * ((1.0f - 0.75f * p) + 2.75f * p * kickShown); }
    std::atomic<bool> glowMoved { false };
    // a colour of full strength at a place on the wheel, washed out by 1 - sat
    static V3 wheel (float hue, float sat)
    {
        const float h = (hue - std::floor (hue)) * 6.0f;
        const float x = 1.0f - std::abs (std::fmod (h, 2.0f) - 1.0f);
        const V3 c = h < 1 ? V3 { 1, x, 0 } : h < 2 ? V3 { x, 1, 0 } : h < 3 ? V3 { 0, 1, x } : h < 4 ? V3 { 0, x, 1 } : h < 5 ? V3 { x, 0, 1 } : V3 { 1, 0, x };
        const float m = 1.0f - sat;
        return { m + c.x * sat, m + c.y * sat, m + c.z * sat };
    }
    std::atomic<int> lookSpread { -1 };         // how far apart the darkest and brightest points of the first picture were (-1: not looked at yet)
    std::atomic<double> lastMs { 0.0 };
    // what the eye last saw through, for finding the pointer's place (under the lock)
    struct Seen { M4 vp, inv; bool ok = false; } seen;
    juce::WaitableEvent captured;
    juce::Image shot;

    // ---- the message thread's own
    Parts partsMsg;
    bool partsGiven = false;
    juce::Image sheetImg;
    static constexpr int kTile = 64;            // the sheet is compared and sent in squares of this many pixels
    juce::HeapBlock<juce::uint8> sentPx;        // the sheet as the drawing thread last had it
    int sheetW = 0, sheetH = 0; float sheetS = 2.0f;
    Pose homePose;
    V3 homeEye, homeAt;                 // home, worked out for this window
    bool following = false;
    juce::Rectangle<float> followBox;

    // ---- the drawing thread's own
    struct Batch
    {
        GLuint vao = 0, vbo = 0, ibo = 0, inst = 0; int count = 0, stride = 0, made = 0; std::vector<float> data;
        int instances() const { return stride > 0 ? (int) data.size() / stride : 0; }
    };
    Batch slabs, capBig, capSmall, baseBig, baseSmall, lens, segBig, segSmall, lensLit;
    GLuint quadVao = 0, quadVbo = 0;
    std::unique_ptr<juce::OpenGLShaderProgram> pSlab, pTurned, pSlabDepth, pTurnedDepth, pLight, pRoom, pGlow, pDown, pUp, pFinal, pCopy, pTag;
    GLuint sheetTex = 0, shadowTex = 0, shadowFbo = 0;
    GLuint msFbo = 0, msColour = 0, msDepth = 0, sceneFbo = 0, sceneTex = 0, finalFbo = 0, finalTex = 0;
    static constexpr int kMips = 6;
    GLuint mipFbo[kMips] {}, mipTex[kMips] {}; int mipW[kMips] {}, mipH[kMips] {};
    int fbW = 0, fbH = 0, samples = 4;
    static constexpr int kShadow = 4096;
    Parts parts;
    bool dark = false, shadowStale = true, sceneStale = true, haveFinal = false, needBuild = false;
    bool gotSheet = false, looked = false, blankForTest = false; int lookedBad = 0;
    juce::uint64 shapeWas = 0;
    struct Live { float v = -2.0f, target = -2.0f; bool gliding = false; };
    std::vector<Live> liveWas;
    juce::uint32 lastMoveMs = 0;
    V3 curEye, curAt, softEye, softAt, goalEye, goalAt, homeEyeGl, homeAtGl;
    bool eyeMoving = false;
    double lastFrameAt = 0.0;
    M4 sunVP;
    struct TagGl { int id = 0; GLuint tex = 0; int w = 0, h = 0; juce::uint32 stamp = 0; Tag tag; float alpha = 0.0f; bool wanted = false; float sx = 0, sy = 0; bool placed = false; };
    std::vector<TagGl> tags;
    float tagS = 2.0f;

    // ================================================================================ messages
    float aspect() const { return comp != nullptr && comp->getHeight() > 0 ? (float) comp->getWidth() / (float) comp->getHeight() : kW / kH; }

    void caseCorners (V3* c) const
    {
        int n = 0;
        for (float x : { -kW * 0.5f + 8.0f, kW * 0.5f - 8.0f }) for (float y : { -Parts::kCase, Parts::kPlate + Parts::kCapBig }) for (float z : { -kH * 0.5f + 12.0f, kH * 0.5f - 13.0f }) c[n++] = { x, y, z };
    }

    // home, for this window: a named view is fitted to it, a placed one is as it was placed
    void resolveHome()
    {
        const V3 side = sideOf (homePose.turn, homePose.tilt);
        if (homePose.view >= 0)
        {
            V3 c[8]; caseCorners (c);
            float d = 0.0f;
            stand (side, c, 8, aspect(), homeAt, d);
            homePose.away = d; homePose.tx = homeAt.x; homePose.ty = homeAt.y; homePose.tz = homeAt.z;
        }
        else homeAt = { homePose.tx, homePose.ty, homePose.tz };
        homeEye = homeAt + side * homePose.away;
    }

    // tell the drawing thread where the eye is to be
    void sendEye (bool jump)
    {
        resolveHome();
        V3 ge = homeEye, ga = homeAt;
        if (following)
        {
            // from home's side, far enough to hold the box
            const float x0 = followBox.getX() - kW * 0.5f, x1 = followBox.getRight() - kW * 0.5f, z0 = followBox.getY() - kH * 0.5f, z1 = followBox.getBottom() - kH * 0.5f;
            V3 c[8]; int n = 0;
            for (float x : { x0, x1 }) for (float y : { 0.0f, Parts::kPlate + Parts::kCapBig }) for (float z : { z0, z1 }) c[n++] = { x, y, z };
            float d = 0.0f;
            stand ((homeEye - homeAt).unit(), c, 8, aspect(), ga, d);
            ge = ga + (homeEye - homeAt).unit() * d;
        }
        {
            const juce::ScopedLock sl (lock);
            pending.homeEye = homeEye; pending.homeAt = homeAt; pending.goalEye = ge; pending.goalAt = ga;
            pending.jump = pending.jump || jump; pending.eyeSet = true;
        }
        kick();
    }

    void kick() { if (context.isAttached()) context.triggerRepaint(); }

    void handleAsyncUpdate() override { if (owner.onState) owner.onState(); }

    void fail (const juce::String& what)
    {
        { const juce::ScopedLock sl (lock); why = what; }
        stateNow = (int) State::failed;
        juce::Logger::writeToLog ("SF-000 3D: " + what);
        triggerAsyncUpdate();
    }

    // ================================================================================== OpenGL
    std::unique_ptr<juce::OpenGLShaderProgram> program (const char* name, const char* vert, const char* frag, std::initializer_list<const char*> attributes)
    {
        auto p = std::make_unique<juce::OpenGLShaderProgram> (context);
        if (! p->addVertexShader (vert) || ! p->addFragmentShader (frag)) { fail (juce::String (name) + " shader: " + p->getLastError()); return nullptr; }
        // (where each input is found is fixed here, so that every program that draws a shape reads it the same way)
        for (const char* a : attributes)
        {
            static const std::pair<const char*, GLuint> where[] = { { "aCorner", 0 }, { "aPos", 0 }, { "aArc", 1 }, { "aNormal", 1 }, { "aProf", 2 }, { "aTop", 4 }, { "aN", 3 },
                                                                     { "iRect", 5 }, { "iPos", 5 }, { "iShape", 6 }, { "iColour", 7 }, { "iMat", 8 }, { "iClip", 9 } };
            for (const auto& w : where) if (std::strcmp (w.first, a) == 0) glBindAttribLocation (p->getProgramID(), w.second, a);
        }
        glBindFragDataLocation (p->getProgramID(), 0, "oColour");
        if (! p->link()) { fail (juce::String (name) + " program: " + p->getLastError()); return nullptr; }
        return p;
    }

    // a shape and the list of where to draw it: per-vertex inputs first, then the per-copy ones
    void makeBatch (Batch& b, const Mesh& m, std::initializer_list<std::pair<GLuint, int>> perVertex, std::initializer_list<std::pair<GLuint, int>> perCopy)
    {
        glGenVertexArrays (1, &b.vao); glBindVertexArray (b.vao);
        glGenBuffers (1, &b.vbo); glBindBuffer (GL_ARRAY_BUFFER, b.vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (m.v.size() * sizeof (float)), m.v.data(), GL_STATIC_DRAW);
        int off = 0;
        for (const auto& a : perVertex)
        {
            glEnableVertexAttribArray (a.first);
            glVertexAttribPointer (a.first, a.second, GL_FLOAT, GL_FALSE, (GLsizei) (m.stride * (int) sizeof (float)), (const void*) (size_t) (off * (int) sizeof (float)));
            off += a.second;
        }
        glGenBuffers (1, &b.ibo); glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, b.ibo);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (m.i.size() * sizeof (juce::uint32)), m.i.data(), GL_STATIC_DRAW);
        b.count = (int) m.i.size();
        glGenBuffers (1, &b.inst); glBindBuffer (GL_ARRAY_BUFFER, b.inst);
        b.stride = 0;
        for (const auto& a : perCopy) b.stride += a.second;
        off = 0;
        for (const auto& a : perCopy)
        {
            glEnableVertexAttribArray (a.first);
            glVertexAttribPointer (a.first, a.second, GL_FLOAT, GL_FALSE, (GLsizei) (b.stride * (int) sizeof (float)), (const void*) (size_t) (off * (int) sizeof (float)));
            glVertexAttribDivisor (a.first, 1);
            off += a.second;
        }
        glBindVertexArray (0);
    }

    void fill (Batch& b)
    {
        glBindBuffer (GL_ARRAY_BUFFER, b.inst);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (b.data.size() * sizeof (float)), b.data.empty() ? nullptr : b.data.data(), GL_DYNAMIC_DRAW);
        b.made = b.instances();
    }

    void draw (const Batch& b)
    {
        if (b.made <= 0) return;
        glBindVertexArray (b.vao);
        glDrawElementsInstanced (GL_TRIANGLES, b.count, GL_UNSIGNED_INT, nullptr, b.made);
    }

    void newOpenGLContextCreated() override
    {
        { const juce::ScopedLock sl (lock); driverName = juce::String ((const char*) glGetString (GL_RENDERER)) + ", OpenGL " + juce::String ((const char*) glGetString (GL_VERSION)); }
        const auto askedToFail = juce::SystemStats::getEnvironmentVariable ("SF000_3D_FAIL", "");
        blankForTest = askedToFail == "blank";          // (for the checks: it starts, and then draws nothing)
        if (askedToFail == "stall") return;             // (for the checks: a context that is made and never draws)
        if (askedToFail.isNotEmpty() && ! blankForTest) { fail ("asked not to start (SF000_3D_FAIL is set)"); return; }
        if (glVertexAttribDivisor == nullptr || glDrawElementsInstanced == nullptr || glBlitFramebuffer == nullptr || glRenderbufferStorageMultisample == nullptr || glGenVertexArrays == nullptr)
        { fail ("this OpenGL (" + driverName + ") is older than 3.2"); return; }

        pSlab = program ("slab", kSlabVert, kSurfaceFrag, { "aCorner", "aArc", "aProf", "aN", "aTop", "iRect", "iShape", "iColour", "iMat", "iClip" });
        pTurned = program ("turned", kTurnedVert, kSurfaceFrag, { "aPos", "aNormal", "aTop", "iPos", "iColour", "iMat", "iClip" });
        pSlabDepth = program ("slab shadow", kSlabVert, kDepthFrag, { "aCorner", "aArc", "aProf", "aN", "aTop", "iRect", "iShape", "iColour", "iMat", "iClip" });
        pTurnedDepth = program ("turned shadow", kTurnedVert, kDepthFrag, { "aPos", "aNormal", "aTop", "iPos", "iColour", "iMat", "iClip" });
        pLight = program ("light", kLightVert, kLightFrag, { "aPos", "iPos", "iColour", "iClip" });
        pRoom = program ("room", kFullVert, kRoomFrag, { "aPos" });
        pGlow = program ("underglow", kGlowVert, kGlowFrag, { "aPos" });
        pDown = program ("glow down", kFullVert, kDownFrag, { "aPos" });
        pUp = program ("glow up", kFullVert, kUpFrag, { "aPos" });
        pFinal = program ("final", kFullVert, kFinalFrag, { "aPos" });
        pCopy = program ("copy", kFullVert, kCopyFrag, { "aPos" });
        pTag = program ("tag", kTagVert, kTagFrag, { "aPos" });
        if (stateNow.load() == (int) State::failed) return;

        const std::initializer_list<std::pair<GLuint, int>> slabVertex { { 0, 2 }, { 1, 2 }, { 2, 3 }, { 3, 2 }, { 4, 1 } }, slabCopy { { 5, 4 }, { 6, 4 }, { 7, 4 }, { 8, 4 }, { 9, 4 } };
        const std::initializer_list<std::pair<GLuint, int>> turnedVertex { { 0, 3 }, { 1, 3 }, { 4, 1 } }, turnedCopy { { 5, 4 }, { 7, 4 }, { 8, 4 }, { 9, 4 } };
        const std::initializer_list<std::pair<GLuint, int>> lightCopy { { 5, 4 }, { 7, 4 }, { 9, 4 } };
        makeBatch (slabs, slabMesh(), slabVertex, slabCopy);
        makeBatch (capBig, turned (kCapBig, 32), turnedVertex, turnedCopy);
        makeBatch (capSmall, turned (kCapSmall, 28), turnedVertex, turnedCopy);
        // a knob's base: a dark disc let into the plate, a hair proud of it
        makeBatch (baseBig, turned ({ { 0.0f, 0.0f }, { 19.5f, 0.0f }, { 19.3f, 0.16f }, { 0.0f, 0.16f } }, 40), turnedVertex, turnedCopy);
        makeBatch (baseSmall, turned ({ { 0.0f, 0.0f }, { 13.65f, 0.0f }, { 13.45f, 0.16f }, { 0.0f, 0.16f } }, 32), turnedVertex, turnedCopy);
        // a lamp's lens: a low dome, one unit across its radius
        const Mesh dome = turned ({ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.96f, 0.26f }, { 0.82f, 0.46f }, { 0.58f, 0.60f }, { 0.30f, 0.66f }, { 0.0f, 0.68f } }, 20);
        makeBatch (lens, dome, turnedVertex, turnedCopy);
        {
            // (the same dome as a light: only where its points are)
            Mesh bare; bare.stride = 3; bare.i = dome.i;
            for (size_t k = 0; k + 7 <= dome.v.size(); k += 7) for (int j = 0; j < 3; ++j) bare.v.push_back (dome.v[k + (size_t) j]);
            makeBatch (lensLit, bare, { { 0, 3 } }, lightCopy);
        }
        makeBatch (segBig, segmentMesh (12.5f, 18.4f, 0.2f, 1.39f), { { 0, 3 } }, lightCopy);
        makeBatch (segSmall, segmentMesh (8.75f, 12.9f, 0.2f, 1.39f), { { 0, 3 } }, lightCopy);

        const float quad[] = { 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 };
        glGenVertexArrays (1, &quadVao); glBindVertexArray (quadVao);
        glGenBuffers (1, &quadVbo); glBindBuffer (GL_ARRAY_BUFFER, quadVbo);
        glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray (0); glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindVertexArray (0);

        // the sheet: four sizes of it, each half the last, so that it stays clean seen small or aslant
        glGenTextures (1, &sheetTex); glBindTexture (GL_TEXTURE_2D, sheetTex);
        for (int l = 0; l < 4; ++l) glTexImage2D (GL_TEXTURE_2D, l, GL_RGBA8, sheetW >> l, sheetH >> l, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (juce::OpenGLHelpers::isExtensionSupported ("GL_EXT_texture_filter_anisotropic"))
        {
            GLfloat most = 1.0f; glGetFloatv (0x84FF, &most);
            glTexParameterf (GL_TEXTURE_2D, 0x84FE, std::min (8.0f, most));
        }
        {
            // (empty to begin with: nothing printed)
            std::vector<juce::uint8> none ((size_t) sheetW * (size_t) sheetH * 4, 0);
            for (int l = 0; l < 4; ++l) glTexSubImage2D (GL_TEXTURE_2D, l, 0, 0, sheetW >> l, sheetH >> l, GL_BGRA, GL_UNSIGNED_BYTE, none.data());
        }

        // the shadows: how far everything is from the light, seen from it
        glGenTextures (1, &shadowTex); glBindTexture (GL_TEXTURE_2D, shadowTex);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadow, kShadow, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glGenFramebuffers (1, &shadowFbo); glBindFramebuffer (GL_FRAMEBUFFER, shadowFbo);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
        glDrawBuffer (GL_NONE); glReadBuffer (GL_NONE);
        if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fail ("the shadow buffer could not be made (" + driverName + ")"); return; }
        glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());

        GLint most = 4; glGetIntegerv (GL_MAX_SAMPLES, &most);
        samples = juce::jlimit (0, 4, (int) most);
        if (const auto s = juce::SystemStats::getEnvironmentVariable ("SF000_SAMPLES", ""); s.isNotEmpty()) samples = juce::jlimit (0, (int) most, s.getIntValue());

        // the light: from behind and to the left, 48 degrees up (its glare on the glass misses the player's seat)
        {
            const V3 sun = sunDir();
            V3 c[8]; caseCorners (c);
            const M4 view = M4::look (sun * 3000.0f, {}, { 0, 1, 0 });
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
            for (const auto& p : c) { const V3 q = view.point (p); x0 = std::min (x0, q.x); x1 = std::max (x1, q.x); y0 = std::min (y0, q.y); y1 = std::max (y1, q.y); z0 = std::min (z0, -q.z); z1 = std::max (z1, -q.z); }
            sunVP = M4::ortho (x0 - 20, x1 + 20, y0 - 20, y1 + 20, z0 - 60, z1 + 60) * view;
        }
        shadowStale = sceneStale = true;
        // (a context made again, as when the window changes screens: what this one drew from is gone, so everything is built and sent again)
        needBuild = true; sheetOverflow = true;
        gotSheet = looked = false; lookedBad = 0; lookSpread = -1;
        stateNow = (int) State::running;
        triggerAsyncUpdate();
    }

    static V3 sunDir() { const float up = rad (48.0f), round = rad (-50.0f); return V3 { std::sin (round) * std::cos (up), std::sin (up), -std::cos (round) * std::cos (up) }.unit(); }

    void openGLContextClosing() override
    {
        for (Batch* b : { &slabs, &capBig, &capSmall, &baseBig, &baseSmall, &lens, &lensLit, &segBig, &segSmall })
        {
            if (b->vao != 0) glDeleteVertexArrays (1, &b->vao);
            for (GLuint* buf : { &b->vbo, &b->ibo, &b->inst }) if (*buf != 0) glDeleteBuffers (1, buf);
            *b = {};
        }
        if (quadVao != 0) glDeleteVertexArrays (1, &quadVao);
        if (quadVbo != 0) glDeleteBuffers (1, &quadVbo);
        quadVao = quadVbo = 0;
        dropBuffers();
        for (GLuint* t : { &sheetTex, &shadowTex }) if (*t != 0) { glDeleteTextures (1, t); *t = 0; }
        if (shadowFbo != 0) { glDeleteFramebuffers (1, &shadowFbo); shadowFbo = 0; }
        for (auto& t : tags) if (t.tex != 0) glDeleteTextures (1, &t.tex);
        tags.clear();
        for (auto* p : { &pSlab, &pTurned, &pSlabDepth, &pTurnedDepth, &pLight, &pRoom, &pGlow, &pDown, &pUp, &pFinal, &pCopy, &pTag }) p->reset();
        haveFinal = false;
    }

    void dropBuffers()
    {
        for (GLuint* f : { &msFbo, &sceneFbo, &finalFbo }) if (*f != 0) { glDeleteFramebuffers (1, f); *f = 0; }
        for (GLuint* r : { &msColour, &msDepth }) if (*r != 0) { glDeleteRenderbuffers (1, r); *r = 0; }
        for (GLuint* t : { &sceneTex, &finalTex }) if (*t != 0) { glDeleteTextures (1, t); *t = 0; }
        for (int i = 0; i < kMips; ++i)
        {
            if (mipFbo[i] != 0) glDeleteFramebuffers (1, &mipFbo[i]);
            if (mipTex[i] != 0) glDeleteTextures (1, &mipTex[i]);
            mipFbo[i] = mipTex[i] = 0;
        }
        fbW = fbH = 0;
    }

    GLuint target (GLuint& fbo, GLenum format, int w, int h, GLenum filter)
    {
        GLuint t = 0;
        glGenTextures (1, &t); glBindTexture (GL_TEXTURE_2D, t);
        glTexImage2D (GL_TEXTURE_2D, 0, (GLint) format, w, h, 0, GL_RGBA, format == (GLenum) GL_RGBA8 ? (GLenum) GL_UNSIGNED_BYTE : (GLenum) GL_HALF_FLOAT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint) filter); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint) filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers (1, &fbo); glBindFramebuffer (GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
        return t;
    }

    // the pictures the frame is built up in, at the window's size
    bool makeBuffers (int w, int h)
    {
        dropBuffers();
        fbW = w; fbH = h;
        for (;;)
        {
            glGenFramebuffers (1, &msFbo); glBindFramebuffer (GL_FRAMEBUFFER, msFbo);
            glGenRenderbuffers (1, &msColour); glBindRenderbuffer (GL_RENDERBUFFER, msColour);
            if (samples > 1) glRenderbufferStorageMultisample (GL_RENDERBUFFER, samples, GL_RGBA16F, w, h); else glRenderbufferStorage (GL_RENDERBUFFER, GL_RGBA16F, w, h);
            glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColour);
            glGenRenderbuffers (1, &msDepth); glBindRenderbuffer (GL_RENDERBUFFER, msDepth);
            if (samples > 1) glRenderbufferStorageMultisample (GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h); else glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
            if (glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) break;
            // (edges smoothed in a buffer of this kind is more than this driver gives: without, then)
            glDeleteFramebuffers (1, &msFbo); glDeleteRenderbuffers (1, &msColour); glDeleteRenderbuffers (1, &msDepth);
            msFbo = msColour = msDepth = 0;
            if (samples <= 1) { fail ("the picture's buffers could not be made (" + driverName + ")"); return false; }
            samples = 0;
        }
        sceneTex = target (sceneFbo, GL_RGBA16F, w, h, GL_LINEAR);
        bool ok = glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        finalTex = target (finalFbo, GL_RGBA8, w, h, GL_NEAREST);
        ok = ok && glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        int mw = w, mh = h;
        for (int i = 0; i < kMips; ++i)
        {
            mw = std::max (1, mw / 2); mh = std::max (1, mh / 2);
            mipW[i] = mw; mipH[i] = mh;
            mipTex[i] = target (mipFbo[i], GL_RGBA16F, mw, mh, GL_LINEAR);
            ok = ok && glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        if (! ok) { fail ("the picture's buffers could not be made (" + driverName + ")"); return false; }
        haveFinal = false; sceneStale = true;
        return true;
    }

    // ------------------------------------------------------------------- the parts, as drawn
    struct Look { Rgb body, cap, base, unlit, lensOff, badge, pocket; float plateRough = 0.82f; };
    Look look() const
    {
        Look l;
        const juce::Colour body (dark ? 0xff24272A : 0xffA4B1B7);
        l.body = lin (body); l.pocket = lin (body.darker (0.55f));
        l.cap = lin (dark ? juce::Colour (0xff232321) : juce::Colour (0xff2B2B28).brighter (0.12f));
        l.base = lin (juce::Colour (dark ? 0xff111211 : 0xff1E1F1D));
        l.unlit = lin (juce::Colour (dark ? 0xff474B48 : 0xff4D514E));
        l.lensOff = lin (juce::Colour (dark ? 0xff3C403D : 0xff6C706A));
        l.badge = lin (juce::Colour (dark ? 0xff151615 : 0xff2B2B28));
        l.plateRough = dark ? 0.7f : 0.82f;
        return l;
    }

    // what a light gives off: its colour, brighter than white can be drawn, and as bright to the eye whatever the colour
    static Rgb glowOf (juce::Colour c)
    {
        const bool white = c.getSaturation() < 0.15f;
        const juce::Colour on = white ? juce::Colour (0xffFFFDF6) : c.withMultipliedSaturation (1.15f).withMultipliedBrightness (1.42f).interpolatedWith (juce::Colours::white, 0.08f);
        const Rgb l = lin (on);
        const float lum = 0.2126f * l.r + 0.7152f * l.g + 0.0722f * l.b;
        return l * ((white ? 1.3f : 1.7f) / std::max (0.05f, lum));
    }

    static void clipTo (std::vector<float>& d, const Parts::Clip& c)
    {
        for (float f : { c.x0 - kW * 0.5f, c.y0 - kH * 0.5f, c.x1 - kW * 0.5f, c.y1 - kH * 0.5f }) d.push_back (f);
    }
    void slab (juce::Rectangle<float> r, float base, float height, float corner, float roll, Rgb colour, float rough, float kind, float emit, float metal, const Parts::Clip& clip, float finish = 0.0f)
    {
        auto& d = slabs.data;
        for (float f : { r.getCentreX() - kW * 0.5f, r.getCentreY() - kH * 0.5f, r.getWidth() * 0.5f, r.getHeight() * 0.5f, base, height, corner, roll,
                         colour.r, colour.g, colour.b, rough, kind, emit, metal, finish }) d.push_back (f);
        clipTo (d, clip);
    }
    void turnedAt (Batch& b, float cx, float cy, float base, float scale, Rgb colour, float rough, float kind, const Parts::Clip& clip)
    {
        for (float f : { cx - kW * 0.5f, cy - kH * 0.5f, base, scale, colour.r, colour.g, colour.b, rough, kind, 0.0f, 0.0f, 0.0f }) b.data.push_back (f);
        clipTo (b.data, clip);
    }
    static void lightAt (Batch& b, float cx, float cy, float base, float angle, Rgb colour, float scale, const Parts::Clip& clip)
    {
        for (float f : { cx - kW * 0.5f, cy - kH * 0.5f, base, angle, colour.r, colour.g, colour.b, scale }) b.data.push_back (f);
        clipTo (b.data, clip);
    }

    // everything that does not change from frame to frame
    void build()
    {
        const Look l = look();
        const Parts::Clip all;
        for (Batch* b : { &slabs, &capBig, &capSmall, &baseBig, &baseSmall, &lens, &lensLit }) b->data.clear();
        if (parts.body) slab ({ 8.0f, 12.0f, kW - 16.0f, kH - 25.0f }, -Parts::kCase, Parts::kCase, 26.0f, 4.5f, l.body, dark ? 0.40f : 0.46f, 4.0f, 0.0f, 0.55f, all);
        for (const auto& p : parts.plates)
        {
            slab (p.r.expanded (1.3f), p.base + 0.05f, 0.02f, 8.6f, 0.0f, l.pocket, 0.9f, 0.0f, 0.0f, 0.0f, p.clip);       // the pocket it stands in, seen as the gap round it
            slab (p.r, p.base, Parts::kPlate, 7.5f, 1.2f, lin (p.ground), l.plateRough, 1.0f, 0.0f, 0.0f, p.clip, 1.0f);       // (a faceplate: it has the anodized finish)
        }
        for (const auto& k : parts.keys)
        {
            const juce::Colour c (k.colour);
            slab (k.r, k.base + 0.04f, 0.02f, 2.6f, 0.0f, lin (juce::Colour (0xff0E0F0E)), 0.9f, 0.0f, 0.0f, 0.0f, k.clip);       // the hole it moves in
            const Rgb shown = lin (k.glows ? c.withMultipliedSaturation (1.1f).withMultipliedBrightness (1.25f) : c);
            slab (k.r.reduced (0.7f), k.base, k.down ? Parts::kKeyDown : Parts::kKey, 2.2f, 1.4f, shown, 0.5f, k.glows ? 3.0f : 1.0f, k.glows ? 0.85f : 0.0f, 0.0f, k.clip);
        }
        for (const auto& p : parts.panes)
        {
            const juce::Colour g (p.glass);
            const bool deep = g.getPerceivedBrightness() < 0.4f;
            slab (p.r, p.base, Parts::kPane, 1.4f, 0.5f, lin (deep ? g.darker (0.35f) : g), 0.2f, 2.0f, 0.0f, 0.0f, p.clip);
        }
        for (const auto& b : parts.badges) slab (b.r, b.base, Parts::kPlate, 5.0f, 1.2f, l.badge, 0.4f, 1.0f, 0.0f, 0.0f, all);
        for (const auto& h : parts.hollows)
        {
            // where a control could be and is not: a shallow dimple, the surface's own colour in shade
            Rgb ground = l.body;
            for (const auto& p : parts.plates) if (p.r.contains (h.cx, h.cy) && p.clip.has (h.cx, h.cy)) ground = lin (p.ground);
            const Rgb dim = ground * (dark ? 0.55f : 0.62f);
            if (h.round) turnedAt (baseBig, h.cx, h.cy, h.base + 0.02f, (h.d * 0.5f - 1.0f) / 19.5f, dim, 0.8f, 0.0f, h.clip);
            else slab (juce::Rectangle<float> (h.cx - h.d * 0.5f, h.cy - h.d * 0.5f, h.d, h.d).reduced (1.0f), h.base + 0.04f, 0.02f, 2.6f, 0.0f, dim, 0.8f, 0.0f, 0.0f, 0.0f, h.clip);
        }
        for (const auto& k : parts.knobs)
        {
            turnedAt (k.big ? baseBig : baseSmall, k.cx, k.cy, k.base + 0.02f, 1.0f, l.base, 0.3f, 0.0f, k.clip);
            turnedAt (k.big ? capBig : capSmall, k.cx, k.cy, k.base, 1.0f, l.cap, dark ? 0.36f : 0.42f, 1.0f, k.clip);
        }
        for (const auto& lp : parts.lamps)
        {
            turnedAt (baseBig, lp.cx, lp.cy, lp.base + 0.02f, lp.d * 0.5f / 19.5f, lin (juce::Colour (0xff1E1F1D)), 0.5f, 0.0f, lp.clip);
            if (lp.on) lightAt (lensLit, lp.cx, lp.cy, lp.base + 0.15f, 0.0f, glowOf (juce::Colour (lp.colour)), lp.d * 0.5f - 2.0f, lp.clip);
            else turnedAt (lens, lp.cx, lp.cy, lp.base + 0.15f, lp.d * 0.5f - 2.0f, l.lensOff, 0.3f, 0.0f, lp.clip);
        }
        for (Batch* b : { &slabs, &capBig, &capSmall, &baseBig, &baseSmall, &lens, &lensLit }) fill (*b);
        liveWas.assign (parts.knobs.size(), {});
        rings (true);
    }

    // The knobs' lights. A knob that shows one of the engine's controls reads it here, on this
    // thread, every frame: its lights follow the sound with nothing in between.
    bool rings (bool force)
    {
        bool changed = force;
        for (size_t i = 0; i < parts.knobs.size(); ++i)
        {
            const auto& k = parts.knobs[i];
            Live now;
            if (k.ctl >= 0 && k.ctl < cx::kCount) { now.v = eng.shown (k.ctl); now.gliding = eng.isGliding (k.ctl); now.target = now.gliding ? eng.target (k.ctl) : -1.0f; }
            if (i < liveWas.size() && (now.v != liveWas[i].v || now.gliding != liveWas[i].gliding || now.target != liveWas[i].target)) { changed = true; liveWas[i] = now; }
        }
        if (! changed) return false;
        const Look l = look();
        const Rgb white = glowOf (juce::Colour (0xffE9E6E2)), green = glowOf (juce::Colour (0xff428F47));
        segBig.data.clear(); segSmall.data.clear();
        for (size_t i = 0; i < parts.knobs.size(); ++i)
        {
            const auto& k = parts.knobs[i];
            float a = k.a, b = k.b, mark = k.mark;
            Rgb on = glowOf (juce::Colour (k.light));
            if (k.ctl >= 0 && k.ctl < cx::kCount && i < liveWas.size())
            {
                const float v = juce::jlimit (0.0f, 1.0f, liveWas[i].v);
                if (k.kind == (int) Kind::Bipolar) { a = std::min (v, 0.5f); b = std::max (v, 0.5f); }
                else if (k.kind == (int) Kind::List)
                {
                    const int n = std::max (2, k.steps), m = (int) std::lround (v * (float) (n - 1));
                    const float per = n <= 16 ? 1.0f / 16.0f : 1.0f / (float) n;
                    a = (float) m * per; b = a + per;
                }
                else { a = 0.0f; b = v; }
                mark = liveWas[i].gliding ? juce::jlimit (0.0f, 0.9999f, liveWas[i].target) : -1.0f;
                if (liveWas[i].gliding) on = green;         // (on its way somewhere: its lights are green while it travels)
            }
            auto& batch = k.big ? segBig : segSmall;
            for (int s = 0; s < 16; ++s)
            {
                const float lo = (float) s / 16.0f, hi = (float) (s + 1) / 16.0f;
                const float part = juce::jlimit (0.0f, 1.0f, (std::min (b, hi) - std::max (a, lo)) * 16.0f);
                const float lit = part > 0.995f ? 1.0f : part > 0.02f ? 0.25f + 0.75f * part : 0.0f;
                Rgb c { l.unlit.r + (on.r - l.unlit.r) * lit, l.unlit.g + (on.g - l.unlit.g) * lit, l.unlit.b + (on.b - l.unlit.b) * lit };
                if (mark >= lo && mark < hi) c = { std::max (c.r, white.r), std::max (c.g, white.g), std::max (c.b, white.b) };       // where it is going
                lightAt (batch, k.cx, k.cy, k.base, -(((float) s + 0.5f) / 16.0f) * 2.0f * kPi, c, 1.0f, k.clip);
            }
        }
        fill (segBig); fill (segSmall);
        return true;
    }

    // ------------------------------------------------------------------------------ a frame
    void useSurface (juce::OpenGLShaderProgram& p, const M4& vp, V3 eye)
    {
        p.use();
        const GLuint id = p.getProgramID();
        auto loc = [id] (const char* n) { return glGetUniformLocation (id, n); };
        glUniformMatrix4fv (loc ("uVP"), 1, GL_FALSE, vp.m);
        glUniform3f (loc ("uEye"), eye.x, eye.y, eye.z);
        const V3 sun = sunDir();
        glUniform3f (loc ("uSun"), sun.x, sun.y, sun.z);
        glUniform3f (loc ("uSunCol"), 2.46f, 2.40f, 2.30f);
        glUniform1f (loc ("uFinish"), dark ? 1.0f : 0.0f);       // (the worn black anodizing is the dark instrument's; the pale one's plates are as they were)
        glUniform3f (loc ("uSky"), 0.40f, 0.42f, 0.44f);
        glUniform3f (loc ("uGround"), 0.20f, 0.17f, 0.15f);
        glUniform1i (loc ("uSheet"), 0);
        glUniform4f (loc ("uSheetMap"), kW * 0.5f, kH * 0.5f, sheetS / (float) sheetW, sheetS / (float) sheetH);
        glUniform1i (loc ("uShadow"), 1);
        glUniformMatrix4fv (loc ("uShadowVP"), 1, GL_FALSE, sunVP.m);
        glUniform1f (loc ("uShadowTexel"), 1.0f / (float) kShadow);
        {
            const float hue = glowHue.load(), gain = glowNow();
            const V3 c = wheel (hue + 0.0202f, 0.90f);
            glUniform3f (loc ("uWarm"), c.x * 0.13f * gain, c.y * 0.13f * gain, c.z * 0.13f * gain);
        }
        glUniform1f (loc ("uCaseDepth"), Parts::kCase);
    }

    void fullPass (juce::OpenGLShaderProgram& p, GLuint fbo, int w, int h)
    {
        glBindFramebuffer (GL_FRAMEBUFFER, fbo);
        glViewport (0, 0, w, h);
        p.use();
        glBindVertexArray (quadVao);
    }

    void renderOpenGL() override
    {
        if (stateNow.load() != (int) State::running) { glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT); return; }
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        GLint vp4[4] {}; glGetIntegerv (GL_VIEWPORT, vp4);
        const int w = std::max (1, (int) vp4[2]), h = std::max (1, (int) vp4[3]);
        if (w != fbW || h != fbH) if (! makeBuffers (w, h)) return;

        // ---- what the other thread has left for this one
        bool wantShot = false, snap = false, gotParts = false, gotTags = false;
        std::vector<SheetUpdate> updates;
        std::vector<Tag> newTags;
        {
            const juce::ScopedLock sl (lock);
            if (pending.hasParts) { parts = std::move (pending.parts); pending.parts = {}; pending.hasParts = false; gotParts = true; }
            if (pending.dark != dark) { dark = pending.dark; gotParts = true; }
            updates.swap (pending.sheet); pending.sheetBytes = 0;
            if (pending.hasTags) { newTags = std::move (pending.tags); pending.tags.clear(); pending.hasTags = false; gotTags = true; }
            if (pending.eyeSet)
            {
                homeEyeGl = pending.homeEye; homeAtGl = pending.homeAt; goalEye = pending.goalEye; goalAt = pending.goalAt;
                if (pending.jump) { curEye = softEye = goalEye; curAt = softAt = goalAt; sceneStale = true; }
                pending.eyeSet = false; pending.jump = false;
            }
            wantShot = pending.capture; pending.capture = false;
            snap = pending.snap; pending.snap = false;
        }
        if (gotParts || needBuild)
        {
            needBuild = false;
            build();
            ++buildCount;
            if (const auto s = parts.shape(); s != shapeWas) { shapeWas = s; shadowStale = true; }
            sceneStale = true;
        }
        if (! updates.empty())
        {
            glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, sheetTex);
            glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
            for (const auto& u : updates) glTexSubImage2D (GL_TEXTURE_2D, u.level, u.r.getX(), u.r.getY(), u.r.getWidth(), u.r.getHeight(), GL_BGRA, GL_UNSIGNED_BYTE, u.px.get());
            sceneStale = true; gotSheet = true;
        }
        if (rings (false)) { sceneStale = true; lastMoveMs = juce::Time::getMillisecondCounter(); }
        if (glowMoved.exchange (false)) sceneStale = true;

        // ---- the eye: toward where it is going, easing off and easing in
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = lastFrameAt > 0.0 ? (float) juce::jlimit (0.0, 0.1, (now - lastFrameAt) * 0.001) : 0.016f;
        lastFrameAt = now;
        if (snap) { curEye = softEye = goalEye; curAt = softAt = goalAt; sceneStale = true; }
        // The glow beats with the kick, if SETUP says so. The kick's meter is the engine's own (it
        // falls 30 dB a second): how near it stands to the loudest it has lately been is the beat,
        // full on a hit and gone 9 dB (0.3 s) later. Only a pulse that has moved draws the scene again.
        bool pulsing = false;
        if (glowPulse.load() > 0.001f)
        {
            const float db = eng.laneDb[0].load();
            kickTop = std::max (db, kickTop - 3.0f * dt);
            const float near = db < -55.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, 1.0f - (kickTop - db) / 9.0f);
            kickEnv = near * near;
            if (std::abs (kickEnv - kickShown) > 0.004f) { kickShown = kickEnv; sceneStale = true; pulsing = true; }
        }
        else if (kickShown != 0.0f) { kickShown = 0.0f; kickTop = -100.0f; sceneStale = true; }
        const float gap = curEye.distance (goalEye) + curAt.distance (goalAt);
        if (gap > 0.05f)
        {
            const float k = 1.0f - std::exp (-dt * 4.2f);
            softEye = softEye.toward (goalEye, k); softAt = softAt.toward (goalAt, k);
            curEye = curEye.toward (softEye, k); curAt = curAt.toward (softAt, k);
            if (curEye.distance (goalEye) + curAt.distance (goalAt) < 0.05f && softEye.distance (goalEye) < 0.05f) { curEye = softEye = goalEye; curAt = softAt = goalAt; }
            sceneStale = true; eyeMoving = true;
        }
        else { eyeMoving = false; softEye = goalEye; softAt = goalAt; }
        away = curEye.distance (homeEyeGl) + curAt.distance (homeAtGl) > 0.5f;

        const float asp = (float) w / (float) h, far = curEye.distance (curAt);
        const M4 view = M4::look (curEye, curAt, { 0, 1, 0 });
        const M4 vp = M4::perspective (kFov, asp, std::max (20.0f, far * 0.08f), far * 4.0f + 6000.0f) * view;
        {
            const juce::ScopedLock sl (lock);
            seen.vp = vp; seen.ok = vp.inverse (seen.inv);
        }

        if (sceneStale || ! haveFinal)
        {
            scene (vp, w, h);
            sceneStale = false; haveFinal = true;
            ++frameCount;
            // the first picture with the instrument in it is looked at: see look()
            if (! looked && gotSheet && slabs.instances() > 0 && w >= 64 && h >= 64)
            {
                juce::String found;
                if (look (w, h, found)) looked = true;
                else if (++lookedBad >= 2) { fail ("the picture came out blank (" + found + "; " + driverName + ")"); return; }
                else { sceneStale = true; context.triggerRepaint(); }       // (once more, to be sure, before giving up)
            }
        }

        // ---- to the window: the finished picture, and the tags over it
        glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());
        glViewport (0, 0, w, h);
        glDisable (GL_DEPTH_TEST); glDisable (GL_BLEND);
        pCopy->use();
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, finalTex);
        glUniform1i (glGetUniformLocation (pCopy->getProgramID(), "uTex"), 0);
        glBindVertexArray (quadVao);
        glDrawArrays (GL_TRIANGLES, 0, 6);
        const bool tagsMoving = drawTags (gotTags, newTags, vp, w, h, dt);
        glBindVertexArray (0);
        gotFrame = true;

        if (wantShot)
        {
            glFinish();
            juce::Image img (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
            std::vector<juce::uint8> px ((size_t) w * (size_t) h * 4);
            glPixelStorei (GL_PACK_ALIGNMENT, 4);
            glReadPixels (0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
            {
                juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
                for (int y = 0; y < h; ++y)
                {
                    const juce::uint8* src = px.data() + (size_t) (h - 1 - y) * (size_t) w * 4;
                    juce::uint8* dst = bd.getLinePointer (y);
                    for (int x = 0; x < w; ++x) { dst[x * 4] = src[x * 4]; dst[x * 4 + 1] = src[x * 4 + 1]; dst[x * 4 + 2] = src[x * 4 + 2]; dst[x * 4 + 3] = 255; }
                }
            }
            { const juce::ScopedLock sl (lock); shot = img; }
            captured.signal();
        }
        lastMs = juce::Time::getMillisecondCounterHiRes() - t0;
       #if ! JUCE_MAC
        // (on a Mac the display itself calls for each frame; elsewhere this thread keeps itself going while anything moves)
        if (eyeMoving || tagsMoving || pulsing || juce::Time::getMillisecondCounter() - lastMoveMs < 400) context.triggerRepaint();
       #else
        juce::ignoreUnused (tagsMoving, pulsing);
       #endif
    }

    // Is there a picture at all? One sum in a shader that comes out as not a number can blank the
    // whole frame on one maker's chips and not on another's (it happened in the study page, on a
    // Mac, and never in software), and a blank screen on stage is worse than the lit skin. So the
    // first picture of each context is read back at a grid of points, before the glow (as numbers)
    // and as finished: an instrument is never one flat colour all over. Twice blank and the stage
    // gives up, says what it found, and the lit skin comes back.
    bool look (int w, int h, juce::String& found)
    {
        constexpr int nx = 9, ny = 7;
        int notNumbers = 0, lo = 255, hi = 0;
        clearErrors();
        glPixelStorei (GL_PACK_ALIGNMENT, 1);
        glBindFramebuffer (GL_READ_FRAMEBUFFER, sceneFbo);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                float f[4] {};
                glReadPixels ((int) (((float) i + 0.5f) * (float) w / (float) nx), (int) (((float) j + 0.5f) * (float) h / (float) ny), 1, 1, GL_RGBA, GL_FLOAT, f);
                for (int k = 0; k < 3; ++k)
                {
                    juce::uint32 bits = 0; std::memcpy (&bits, &f[k], 4);
                    if ((bits & 0x7f800000u) == 0x7f800000u) { ++notNumbers; break; }       // (asked of the bits: the build's quick sums may not tell)
                }
            }
        glBindFramebuffer (GL_READ_FRAMEBUFFER, finalFbo);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                juce::uint8 b[4] {};
                glReadPixels ((int) (((float) i + 0.5f) * (float) w / (float) nx), (int) (((float) j + 0.5f) * (float) h / (float) ny), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, b);
                const int v = std::max ({ (int) b[0], (int) b[1], (int) b[2] });
                lo = std::min (lo, v); hi = std::max (hi, v);
            }
        glBindFramebuffer (GL_READ_FRAMEBUFFER, 0);
        // (if the reading back itself was refused, nothing was learned: the picture is taken on trust, not thrown away)
        if (glGetError() != GL_NO_ERROR) { clearErrors(); lookSpread = -2; return true; }
        lookSpread = hi - lo;
        found = "its darkest point " + juce::String (lo) + " and its brightest " + juce::String (hi) + " of 255, " + juce::String (notNumbers) + " of " + juce::String (nx * ny) + " points not numbers before the glow";
        return hi - lo >= 6;
    }

    static void clearErrors() { for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) {} }

    void scene (const M4& vp, int w, int h)
    {
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, sheetTex);
        glDisable (GL_BLEND); glDisable (GL_CULL_FACE);
        glFrontFace (GL_CW);            // (the shapes here are wound that way round: the outside of each is its front)
        glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDepthMask (GL_TRUE);

        if (shadowStale)
        {
            glBindFramebuffer (GL_FRAMEBUFFER, shadowFbo);
            glViewport (0, 0, kShadow, kShadow);
            glClear (GL_DEPTH_BUFFER_BIT);
            glEnable (GL_POLYGON_OFFSET_FILL); glPolygonOffset (1.5f, 2.0f);
            pSlabDepth->use(); glUniformMatrix4fv (glGetUniformLocation (pSlabDepth->getProgramID(), "uVP"), 1, GL_FALSE, sunVP.m);
            draw (slabs);
            pTurnedDepth->use(); glUniformMatrix4fv (glGetUniformLocation (pTurnedDepth->getProgramID(), "uVP"), 1, GL_FALSE, sunVP.m);
            draw (capBig); draw (capSmall); draw (lens);
            glDisable (GL_POLYGON_OFFSET_FILL);
            shadowStale = false;
        }
        glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, shadowTex);
        glActiveTexture (GL_TEXTURE0);

        glBindFramebuffer (GL_FRAMEBUFFER, msFbo);
        glViewport (0, 0, w, h);
        glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // the room, then the glow under the case, then the instrument over both
        glDisable (GL_DEPTH_TEST); glDepthMask (GL_FALSE);
        pRoom->use();
        glUniform1f (glGetUniformLocation (pRoom->getProgramID(), "uAspect"), (float) w / (float) h);
        glUniform3f (glGetUniformLocation (pRoom->getProgramID(), "uNear"), 0.0180f, 0.0150f, 0.0140f);
        glUniform3f (glGetUniformLocation (pRoom->getProgramID(), "uFar"), 0.0018f, 0.0018f, 0.0022f);
        glBindVertexArray (quadVao); glDrawArrays (GL_TRIANGLES, 0, 6);
        glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE);
        pGlow->use();
        {
            const GLuint id = pGlow->getProgramID();
            glUniformMatrix4fv (glGetUniformLocation (id, "uVP"), 1, GL_FALSE, vp.m);
            glUniform4f (glGetUniformLocation (id, "uPlane"), 0.0f, 0.0f, kW * 1.1f, kH * 1.35f);
            glUniform1f (glGetUniformLocation (id, "uY"), -Parts::kCase - 80.0f);
            glUniform4f (glGetUniformLocation (id, "uBox"), 0.0f, 0.0f, kW * 0.5f * 0.9f, kH * 0.5f * 0.86f);
            const float gain = glowNow();
            const V3 c = wheel (glowHue.load(), 0.973f);                    // (as it comes: warm, and dim: it is felt more than seen)
            glUniform3f (glGetUniformLocation (id, "uGlow"), c.x * 0.115f * gain, c.y * 0.115f * gain, c.z * 0.115f * gain);
            glUniform1f (glGetUniformLocation (id, "uSoft"), 190.0f);
        }
        glDrawArrays (GL_TRIANGLES, 0, 6);
        glDisable (GL_BLEND);
        glEnable (GL_DEPTH_TEST); glDepthMask (GL_TRUE);

        useSurface (*pSlab, vp, curEye); draw (slabs);
        useSurface (*pTurned, vp, curEye); draw (baseBig); draw (baseSmall); draw (capBig); draw (capSmall); draw (lens);
        pLight->use(); glUniformMatrix4fv (glGetUniformLocation (pLight->getProgramID(), "uVP"), 1, GL_FALSE, vp.m);
        draw (segBig); draw (segSmall); draw (lensLit);

        // the smoothed picture, as one layer
        glBindFramebuffer (GL_READ_FRAMEBUFFER, msFbo); glBindFramebuffer (GL_DRAW_FRAMEBUFFER, sceneFbo);
        glBlitFramebuffer (0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // the glow: down, and back up
        glDisable (GL_DEPTH_TEST); glDepthMask (GL_FALSE);
        GLuint from = sceneTex; int fw = w, fh = h;
        for (int i = 0; i < kMips; ++i)
        {
            fullPass (*pDown, mipFbo[i], mipW[i], mipH[i]);
            glBindTexture (GL_TEXTURE_2D, from);
            glUniform1i (glGetUniformLocation (pDown->getProgramID(), "uTex"), 0);
            glUniform2f (glGetUniformLocation (pDown->getProgramID(), "uTexel"), 1.0f / (float) fw, 1.0f / (float) fh);
            glUniform1f (glGetUniformLocation (pDown->getProgramID(), "uKnee"), i == 0 ? 1.05f : 0.0f);
            glDrawArrays (GL_TRIANGLES, 0, 6);
            from = mipTex[i]; fw = mipW[i]; fh = mipH[i];
        }
        glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE);
        // (how much each size counts: the smallest and softest least, so that the glow hugs its light)
        static const float kWeight[kMips] = { 1.0f, 0.75f, 0.5f, 0.32f, 0.2f, 0.12f };
        for (int i = kMips - 1; i > 0; --i)
        {
            fullPass (*pUp, mipFbo[i - 1], mipW[i - 1], mipH[i - 1]);
            glBindTexture (GL_TEXTURE_2D, mipTex[i]);
            glUniform1i (glGetUniformLocation (pUp->getProgramID(), "uTex"), 0);
            glUniform2f (glGetUniformLocation (pUp->getProgramID(), "uTexel"), 1.0f / (float) mipW[i], 1.0f / (float) mipH[i]);
            glUniform1f (glGetUniformLocation (pUp->getProgramID(), "uGain"), kWeight[i] / kWeight[i - 1]);
            glDrawArrays (GL_TRIANGLES, 0, 6);
        }
        glDisable (GL_BLEND);

        fullPass (*pFinal, finalFbo, w, h);
        glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, mipTex[0]);
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, sceneTex);
        glUniform1i (glGetUniformLocation (pFinal->getProgramID(), "uTex"), 0);
        glUniform1i (glGetUniformLocation (pFinal->getProgramID(), "uBloom"), 1);
        glUniform1f (glGetUniformLocation (pFinal->getProgramID(), "uBloomGain"), 0.34f);
        glUniform1f (glGetUniformLocation (pFinal->getProgramID(), "uExposure"), blankForTest ? 0.0f : 1.0f);
        glDrawArrays (GL_TRIANGLES, 0, 6);
        glDepthMask (GL_TRUE);
        glFrontFace (GL_CCW);
    }

    // The tags, over the finished picture. Each keeps the place it has on the window while it is
    // up, eases to where it should be, and fades in and out. True while any of that is going on.
    bool drawTags (bool got, std::vector<Tag>& fresh, const M4& vp, int w, int h, float dt)
    {
        if (got)
        {
            for (auto& t : tags) t.wanted = false;
            for (auto& f : fresh)
            {
                TagGl* mine = nullptr;
                for (auto& t : tags) if (t.id == f.id) mine = &t;
                if (mine == nullptr) { tags.push_back ({}); mine = &tags.back(); mine->id = f.id; }
                mine->wanted = true;
                if (mine->tex == 0 || mine->stamp != f.stamp || mine->w != f.image.getWidth() || mine->h != f.image.getHeight())
                {
                    if (mine->tex == 0) glGenTextures (1, &mine->tex);
                    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, mine->tex);
                    const juce::Image::BitmapData bd (f.image, juce::Image::BitmapData::readOnly);
                    std::vector<juce::uint8> px ((size_t) bd.width * (size_t) bd.height * 4);
                    for (int y = 0; y < bd.height; ++y) std::memcpy (px.data() + (size_t) y * (size_t) bd.width * 4, bd.getLinePointer (y), (size_t) bd.width * 4);
                    glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
                    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, bd.width, bd.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
                    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    mine->w = bd.width; mine->h = bd.height; mine->stamp = f.stamp;
                }
                mine->tag = std::move (f);
                mine->tag.image = {};
            }
        }
        bool moving = false;
        if (tags.empty()) return false;
        glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        pTag->use();
        glUniform1i (glGetUniformLocation (pTag->getProgramID(), "uTex"), 0);
        const float scale = (float) context.getRenderingScale();
        for (auto& t : tags)
        {
            const float want = t.wanted ? 1.0f : 0.0f;
            if (t.alpha != want) { t.alpha = want > t.alpha ? std::min (1.0f, t.alpha + dt * 12.0f) : std::max (0.0f, t.alpha - dt * 4.0f); moving = true; }
            if (t.alpha <= 0.0f || t.tex == 0) continue;
            // where it goes, in the window's pixels (y down)
            const float tw = (float) t.w * scale / tagS, th = (float) t.h * scale / tagS;
            float x = 0.0f, y = 0.0f;
            if (t.tag.place == Tag::fixed) { x = t.tag.px.x * scale - tw * 0.5f; y = t.tag.px.y * scale - th; }
            else if (t.tag.place == Tag::byPointer)
            {
                x = t.tag.px.x * scale + 18.0f * scale; y = t.tag.px.y * scale + 22.0f * scale;
                if (x + tw > (float) w - 6.0f) x = t.tag.px.x * scale - 14.0f * scale - tw;
                if (y + th > (float) h - 6.0f) y = t.tag.px.y * scale - 14.0f * scale - th;
            }
            else
            {
                // just clear of the thing's highest point on the window: the far rim of its top, or of what lies round its foot
                const V3 back = V3 { (curAt - curEye).x, 0.0f, (curAt - curEye).z }.length() > 1.0e-3f ? V3 { (curAt - curEye).x, 0.0f, (curAt - curEye).z }.unit() : V3 { 0, 0, -1 };
                const V3 mid { t.tag.x - kW * 0.5f, 0.0f, t.tag.y - kH * 0.5f };
                float wa = 1.0f, wb = 1.0f;
                const V3 a = vp.point (mid + V3 { 0, t.tag.height, 0 } + back * (t.tag.reach * 0.6f), &wa), b = vp.point (mid + back * t.tag.reach, &wb);
                if (wa <= 0.0f || wb <= 0.0f) continue;
                const float px = (a.x * 0.5f + 0.5f) * (float) w, py = (1.0f - std::max (a.y, b.y)) * 0.5f * (float) h;
                x = px - tw * 0.5f; y = py - 8.0f * scale - th;
                if (y < 6.0f)
                {
                    const V3 f = vp.point (mid - back * (t.tag.reach * 1.1f));
                    y = (1.0f - f.y) * 0.5f * (float) h + 8.0f * scale;
                }
            }
            x = juce::jlimit (6.0f, std::max (6.0f, (float) w - tw - 6.0f), x); y = juce::jlimit (6.0f, std::max (6.0f, (float) h - th - 6.0f), y);
            if (! t.placed) { t.sx = x; t.sy = y; t.placed = true; }
            else if (std::abs (t.sx - x) + std::abs (t.sy - y) > 0.3f) { const float k = 1.0f - std::exp (-dt * 30.0f); t.sx += (x - t.sx) * k; t.sy += (y - t.sy) * k; moving = true; }
            glBindTexture (GL_TEXTURE_2D, t.tex);
            glUniform4f (glGetUniformLocation (pTag->getProgramID(), "uRect"), std::round (t.sx) / (float) w * 2.0f - 1.0f, 1.0f - (std::round (t.sy) + th) / (float) h * 2.0f, tw / (float) w * 2.0f, th / (float) h * 2.0f);
            glUniform1f (glGetUniformLocation (pTag->getProgramID(), "uAlpha"), t.alpha);
            glDrawArrays (GL_TRIANGLES, 0, 6);
        }
        glDisable (GL_BLEND);
        for (size_t i = tags.size(); i-- > 0;)
            if (! tags[i].wanted && tags[i].alpha <= 0.0f) { if (tags[i].tex != 0) glDeleteTextures (1, &tags[i].tex); tags.erase (tags.begin() + (long) i); }
        return moving;
    }
};

// ========================================================================================= Stage
Stage::Stage (Engine& e) : impl (std::make_unique<Impl> (*this, e)) {}
Stage::~Stage() { detach(); }

void Stage::attachTo (juce::Component& c)
{
    if (impl->context.isAttached()) return;
    impl->context.detach();             // (an attachment whose context was never made is not "attached", but is in the way of another try)
    impl->comp = &c;
    impl->stateNow = (int) State::starting;
    impl->context.setRenderer (impl.get());
    impl->context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    impl->context.setComponentPaintingEnabled (false);
    juce::OpenGLPixelFormat pf; pf.depthBufferBits = 0; pf.stencilBufferBits = 0;
    impl->context.setPixelFormat (pf);
   #if JUCE_MAC
    impl->context.setContinuousRepainting (true);       // the display calls for each frame; a frame with nothing new in it only shows the last again
   #else
    impl->context.setContinuousRepainting (false);
   #endif
    impl->context.setSwapInterval (1);
    impl->sheetOverflow = true;         // (a new picture starts with an empty sheet: all of it is sent again)
    impl->context.attachTo (c);
    impl->sendEye (true);
}

void Stage::detach()
{
    impl->context.detach();             // (whether or not its context was ever made)
    impl->comp = nullptr;
    impl->gotFrame = false;
}

bool Stage::attached() const { return impl->context.isAttached(); }
Stage::State Stage::state() const { return (State) impl->stateNow.load(); }
juce::String Stage::whyNot() const { const juce::ScopedLock sl (impl->lock); return impl->why; }
void Stage::giveUp (const juce::String& why)
{
    { const juce::ScopedLock sl (impl->lock); impl->why = why; }
    impl->stateNow = (int) State::failed;
    juce::Logger::writeToLog ("SF-000 3D: " + why);
}
juce::String Stage::driver() const { const juce::ScopedLock sl (impl->lock); return impl->driverName; }

void Stage::setParts (const Parts& p)
{
    // the same as it has: nothing is handed over, built or sent (the screen records itself again at
    // every refresh, and most refreshes, a meter moving, change no part)
    if (impl->partsGiven && impl->partsMsg.same (p)) return;
    impl->partsGiven = true;
    impl->partsMsg = p;
    {
        const juce::ScopedLock sl (impl->lock);
        impl->pending.parts = p; impl->pending.hasParts = true;
    }
    impl->kick();
}

void Stage::setGlow (float hue, float bright, float pulse)
{
    const float b = juce::jlimit (0.0f, 1.0f, bright);
    impl->glowHue = hue - std::floor (hue);
    impl->glowPulse = juce::jlimit (0.0f, 1.0f, pulse);
    impl->glowGain = 4.0f * b * b;
    impl->glowMoved = true;
    impl->kick();
}

void Stage::setDark (bool on) { { const juce::ScopedLock sl (impl->lock); impl->pending.dark = on; } impl->kick(); }

juce::Image& Stage::sheet() { return impl->sheetImg; }
float Stage::sheetScale() const { return impl->sheetS; }

// Only what is different from what the drawing thread already has is sent: the sheet is compared,
// square by square, with a copy of what was last sent. (A whole repaint of the screen that moves one
// number then sends one square, not thirty megabytes.)
void Stage::sheetChanged (juce::Rectangle<int> r)
{
    auto& im = *impl;
    const int T = Impl::kTile;
    const bool all = im.sheetOverflow.exchange (false);        // it has lost track of what it has: everything again
    if (all) r = { 0, 0, im.sheetW, im.sheetH };
    r = r.getIntersection ({ 0, 0, im.sheetW, im.sheetH });
    if (r.isEmpty()) return;
    const int tx0 = r.getX() / T, ty0 = r.getY() / T, tx1 = (r.getRight() - 1) / T, ty1 = (r.getBottom() - 1) / T;
    const juce::Image::BitmapData bd (im.sheetImg, juce::Image::BitmapData::readOnly);
    const size_t stride = (size_t) im.sheetW * 4;
    std::vector<Impl::SheetUpdate> made;
    size_t bytes = 0;
    auto send = [&] (int tx, int count, int ty)
    {
        // a run of changed squares: taken into the copy, and sent with its three smaller sizes
        const juce::Rectangle<int> box (tx * T, ty * T, count * T, T);
        Impl::SheetUpdate u; u.level = 0; u.r = box; u.px.malloc ((size_t) box.getWidth() * (size_t) T * 4);
        for (int y = 0; y < T; ++y)
        {
            const juce::uint8* src = bd.getLinePointer (box.getY() + y) + (size_t) box.getX() * 4;
            std::memcpy (im.sentPx.get() + (size_t) (box.getY() + y) * stride + (size_t) box.getX() * 4, src, (size_t) box.getWidth() * 4);
            std::memcpy (u.px.get() + (size_t) y * (size_t) box.getWidth() * 4, src, (size_t) box.getWidth() * 4);
        }
        bytes += (size_t) box.getWidth() * (size_t) T * 4;
        made.push_back (std::move (u));
        for (int l = 1; l < 4; ++l)
        {
            const auto& up = made.back();
            Impl::SheetUpdate d; d.level = l; d.r = { up.r.getX() / 2, up.r.getY() / 2, up.r.getWidth() / 2, up.r.getHeight() / 2 };
            const int w = d.r.getWidth(), h = d.r.getHeight(), uw = up.r.getWidth();
            d.px.malloc ((size_t) w * (size_t) h * 4);
            for (int y = 0; y < h; ++y)
            {
                // four pixels to one, all four channels at once (two at a time, in the halves of a word)
                const juce::uint32* a = (const juce::uint32*) (up.px.get() + (size_t) (y * 2) * (size_t) uw * 4), *b = a + uw;
                juce::uint32* o = (juce::uint32*) (d.px.get() + (size_t) y * (size_t) w * 4);
                for (int x = 0; x < w; ++x)
                {
                    const juce::uint32 p = a[x * 2], q = a[x * 2 + 1], s = b[x * 2], t = b[x * 2 + 1];
                    const juce::uint32 lo = (p & 0x00ff00ffu) + (q & 0x00ff00ffu) + (s & 0x00ff00ffu) + (t & 0x00ff00ffu) + 0x00020002u;
                    const juce::uint32 hi = ((p >> 8) & 0x00ff00ffu) + ((q >> 8) & 0x00ff00ffu) + ((s >> 8) & 0x00ff00ffu) + ((t >> 8) & 0x00ff00ffu) + 0x00020002u;
                    o[x] = ((lo >> 2) & 0x00ff00ffu) | (((hi >> 2) & 0x00ff00ffu) << 8);
                }
            }
            bytes += (size_t) w * (size_t) h * 4;
            made.push_back (std::move (d));
        }
    };
    for (int ty = ty0; ty <= ty1; ++ty)
    {
        int run = -1;
        for (int tx = tx0; tx <= tx1 + 1; ++tx)
        {
            bool changed = false;
            if (tx <= tx1)
            {
                changed = all;
                for (int y = 0; y < T && ! changed; ++y)
                    changed = std::memcmp (bd.getLinePointer (ty * T + y) + (size_t) tx * T * 4, im.sentPx.get() + (size_t) (ty * T + y) * stride + (size_t) tx * T * 4, (size_t) T * 4) != 0;
            }
            if (changed) { if (run < 0) run = tx; }
            else if (run >= 0) { send (run, tx - run, ty); run = -1; }
        }
    }
    if (made.empty()) return;
    {
        const juce::ScopedLock sl (im.lock);
        im.pending.sheetBytes += bytes;
        for (auto& u : made) im.pending.sheet.push_back (std::move (u));
        // (if the drawing thread is not taking them, they are not left to pile up: everything is sent again when it is)
        if (im.pending.sheetBytes > (size_t) 400 * 1024 * 1024) { im.pending.sheet.clear(); im.pending.sheetBytes = 0; im.sheetOverflow = true; }
    }
    im.kick();
}

void Stage::setTags (std::vector<Tag> t)
{
    { const juce::ScopedLock sl (impl->lock); impl->pending.tags = std::move (t); impl->pending.hasTags = true; }
    impl->kick();
}
float Stage::tagScale() const { return impl->tagS; }

void Stage::setHome (const Pose& p, bool fly) { impl->homePose = p; impl->sendEye (! fly); }
Stage::Pose Stage::home() const { return impl->homePose; }

void Stage::turn (float dx, float dy)
{
    auto& p = impl->homePose;
    impl->resolveHome();
    p.view = -1;
    p.turn = std::fmod (p.turn - dx * 0.25f + 540.0f, 360.0f) - 180.0f;
    p.tilt = juce::jlimit (6.0f, 89.9f, p.tilt + dy * 0.25f);
    impl->sendEye (true);
}

void Stage::slide (float dx, float dy)
{
    auto& im = *impl;
    im.resolveHome();
    auto& p = im.homePose;
    p.view = -1;
    const V3 f = (im.homeAt - im.homeEye).unit(), s = f.cross ({ 0, 1, 0 }).unit(), u = s.cross (f);
    const float perPoint = 2.0f * std::tan (kFov * 0.5f) * p.away / (float) std::max (1, im.comp != nullptr ? im.comp->getHeight() : 1000);
    const V3 at = im.homeAt - s * (dx * perPoint) + u * (dy * perPoint);
    p.tx = at.x; p.ty = at.y; p.tz = at.z;
    im.sendEye (true);
}

void Stage::closer (float clicks)
{
    auto& p = impl->homePose;
    impl->resolveHome();
    p.view = -1;
    p.away = juce::jlimit (260.0f, 16000.0f, p.away * std::pow (0.9f, clicks));
    impl->sendEye (true);
}

void Stage::lookAt (const juce::Rectangle<float>* box)
{
    auto& im = *impl;
    const bool was = im.following;
    const auto old = im.followBox;
    im.following = box != nullptr;
    if (box != nullptr) im.followBox = *box;
    if (was != im.following || (im.following && old != im.followBox)) im.sendEye (false);
}

bool Stage::isAway() const { return impl->following || impl->away.load(); }
bool Stage::ready() const { return impl->gotFrame.load(); }

juce::Point<float> Stage::pointsOf (float ux, float uy, float height) const
{
    const auto& im = *impl;
    M4 vp;
    { const juce::ScopedLock sl (im.lock); if (! im.seen.ok) return {}; vp = im.seen.vp; }
    const V3 p = vp.point ({ ux - kW * 0.5f, height, uy - kH * 0.5f });
    const float w = im.comp != nullptr ? (float) im.comp->getWidth() : kW, h = im.comp != nullptr ? (float) im.comp->getHeight() : kH;
    return { (p.x * 0.5f + 0.5f) * w, (1.0f - (p.y * 0.5f + 0.5f)) * h };
}

juce::Point<float> Stage::unitsAt (juce::Point<float> pt) const
{
    const auto& im = *impl;
    M4 inv;
    { const juce::ScopedLock sl (im.lock); if (! im.seen.ok) return { -1.0f, -1.0f }; inv = im.seen.inv; }
    const float w = im.comp != nullptr ? (float) im.comp->getWidth() : kW, h = im.comp != nullptr ? (float) im.comp->getHeight() : kH;
    const float nx = pt.x / std::max (1.0f, w) * 2.0f - 1.0f, ny = 1.0f - pt.y / std::max (1.0f, h) * 2.0f;
    const V3 a = inv.point ({ nx, ny, -1.0f }), b = inv.point ({ nx, ny, 1.0f }), d = (b - a).unit();
    // the nearest thing the line from the eye through the pointer meets
    float best = 1.0e30f;
    juce::Point<float> found (-1.0e6f, -1.0e6f);
    auto level = [&] (float y, float& t, float& ux, float& uy)
    {
        if (std::abs (d.y) < 1.0e-6f) return false;
        t = (y - a.y) / d.y;
        if (t <= 0.0f) return false;
        ux = a.x + d.x * t + kW * 0.5f; uy = a.z + d.z * t + kH * 0.5f;
        return true;
    };
    float t = 0, ux = 0, uy = 0;
    if (level (0.0f, t, ux, uy)) { best = t; found = { ux, uy }; }                  // the case's top, or the air beside it
    const auto& ps = im.partsMsg;
    for (const auto& p : ps.plates)
        if (level (p.base + Parts::kPlate, t, ux, uy) && t < best && p.r.contains (ux, uy) && p.clip.has (ux, uy)) { best = t; found = { ux, uy }; }
    for (const auto& p : ps.panes)
        if (level (p.base + Parts::kPane, t, ux, uy) && t < best && p.r.contains (ux, uy) && p.clip.has (ux, uy)) { best = t; found = { ux, uy }; }
    for (const auto& k : ps.keys)
        if (level (k.base + (k.down ? Parts::kKeyDown : Parts::kKey), t, ux, uy) && t < best && k.r.contains (ux, uy) && k.clip.has (ux, uy)) { best = t; found = { ux, uy }; }
    for (const auto& k : ps.knobs)
    {
        // a cap: an upright cylinder. Its side or its top, whichever the line meets first.
        if (! k.clip.has (k.cx, k.cy)) continue;
        const float r = (k.big ? Parts::kCapRBig : Parts::kCapRSmall) + 1.0f, y0 = k.base, y1 = k.base + (k.big ? Parts::kCapBig : Parts::kCapSmall);
        const float ox = a.x - (k.cx - kW * 0.5f), oz = a.z - (k.cy - kH * 0.5f);
        const float qa = d.x * d.x + d.z * d.z, qb = 2.0f * (ox * d.x + oz * d.z), qc = ox * ox + oz * oz - r * r;
        float hit = -1.0f;
        if (qa > 1.0e-9f)
        {
            const float disc = qb * qb - 4.0f * qa * qc;
            if (disc >= 0.0f)
            {
                const float tt = (-qb - std::sqrt (disc)) / (2.0f * qa), y = a.y + d.y * tt;
                if (tt > 0.0f && y >= y0 && y <= y1) hit = tt;
            }
        }
        if (hit < 0.0f && std::abs (d.y) > 1.0e-6f)
        {
            const float tt = (y1 - a.y) / d.y, px = ox + d.x * tt, pz = oz + d.z * tt;
            if (tt > 0.0f && px * px + pz * pz <= r * r) hit = tt;
        }
        if (hit > 0.0f && hit < best) { best = hit; found = { k.cx, k.cy }; }
    }
    return found;
}

float Stage::heightAt (float ux, float uy) const
{
    const auto& ps = impl->partsMsg;
    float top = 0.0f;
    for (const auto& p : ps.plates) if (p.r.contains (ux, uy) && p.clip.has (ux, uy)) top = std::max (top, p.base + Parts::kPlate);
    for (const auto& p : ps.panes) if (p.r.contains (ux, uy) && p.clip.has (ux, uy)) top = std::max (top, p.base + Parts::kPane);
    for (const auto& k : ps.keys) if (k.r.contains (ux, uy) && k.clip.has (ux, uy)) top = std::max (top, k.base + (k.down ? Parts::kKeyDown : Parts::kKey));
    for (const auto& k : ps.knobs)
    {
        const float r = k.big ? Parts::kCapRBig : Parts::kCapRSmall, dx = ux - k.cx, dy = uy - k.cy;
        if (dx * dx + dy * dy <= r * r && k.clip.has (k.cx, k.cy)) top = std::max (top, k.base + (k.big ? Parts::kCapBig : Parts::kCapSmall));
    }
    return top;
}

void Stage::kick() { impl->kick(); }
void Stage::settle() { { const juce::ScopedLock sl (impl->lock); impl->pending.snap = true; } impl->kick(); }

juce::Image Stage::capture (int timeoutMs)
{
    auto& im = *impl;
    if (! im.context.isAttached()) return {};
    im.captured.reset();
    { const juce::ScopedLock sl (im.lock); im.pending.capture = true; }
    im.kick();
    if (! im.captured.wait (timeoutMs)) return {};
    const juce::ScopedLock sl (im.lock);
    return im.shot;
}

double Stage::frameMs() const { return impl->lastMs.load(); }
int Stage::frames() const { return impl->frameCount.load(); }
int Stage::builds() const { return impl->buildCount.load(); }
int Stage::firstPictureSpread() const { return impl->lookSpread.load(); }
} // namespace sf0
