// (SF-000, the hundred and second build) KICK FORGE on every lane: see ForgeEngine.h and Forge.h.
#include "ForgeEngine.h"
#include "Calibration.h"

namespace sf
{
namespace frg
{
std::vector<ParamSpec> params()
{
    // KICK's own (the same ids, the same specs: Drums.cpp), then the five KICK has as the lane's
    std::vector<ParamSpec> p;
    auto add = [&p] (ParamSpec s) { p.push_back (s.noPreset()); };
    add (pct ("fg_char", "CHARACTER", 0.3f).next().slow().engineGroup());
    add (pct ("fg_impact", "IMPACT", 0.5f).next().engineGroup());
    add (pct ("fg_body", "BODY", 0.5f).engineGroup());
    add (pct ("fg_ground", "GROUND", 0.5f).slow().engineGroup());
    add (pct ("fg_pos", "POSITION", 0.1f).next().engineGroup());
    add (pct ("fg_tone", "TONE", 0.5f).slow().engineGroup());
    add (knob ("fg_glen", "GROUND LENGTH", 0.0f, 1.0f, 0.5f, "BIP").slow().engineGroup());
    add (pct ("fg_low", "LOW PRESERVE", 0.0f).slow().engineGroup());
    add (pct ("fg_hang", "HANG", 0.0f).next().engineGroup());
    add (knob ("fg_curve", "CURVE", 0.0f, 1.0f, 0.5f, "BIP").next().engineGroup());
    add (pct ("fg_hold", "HOLD", 0.0f).next().engineGroup());
    add (knob ("fg_hdec", "HARM DECAY", 0.0f, 1.0f, 0.5f, "BIP").next().engineGroup());
    add (pct ("fg_phase", "PHASE", 0.0f).next().engineGroup());
    add (toggle ("fg_flip", "POLARITY").next().engineGroup());
    add (knob ("fg_ilen", "IMPACT LENGTH", 0.0f, 1.0f, 0.5f, "BIP").next().engineGroup());
    add (knob ("fg_ipitch", "IMPACT PITCH", -24.0f, 24.0f, 0.0f, "st").next().engineGroup());
    add (knob ("fg_icut", "IMPACT CUT", 0.0f, 1.0f, 0.5f, "BIP").next().engineGroup());
    add (knob ("fg_iwidth", "IMPACT WIDTH", 0.0f, 1.0f, 0.5f, "BIP").next().engineGroup());
    add (pct ("fg_vary", "VARIATION", 0.5f).next().engineGroup());
    add (momentary ("fg_solo", "IMPACT ONLY").engineGroup());
    add (knob ("fg_gdark", "GROUND DARK", 0.0f, 1.0f, 0.5f, "BIP").slow().engineGroup());
    add (pct ("fg_gduck", "GROUND DUCK", 0.5f).slow().engineGroup());
    add (knob ("fg_grec", "GROUND RECOVER", 0.0f, 1.0f, 0.5f, "BIP").slow().engineGroup());
    add (pct ("fg_gwidth", "GROUND WIDTH", 0.0f).slow().engineGroup());
    add (knob ("fg_tune", "TUNE", -24.0f, 24.0f, 0.0f, "st").next().engineGroup());
    add (pct ("fg_drive", "DRIVE", 0.2f).engineGroup());
    add (pct ("fg_punch", "PUNCH", 0.5f).next().engineGroup());
    add (pct ("fg_ptime", "PITCH TIME", 0.5f).next().slow().engineGroup());
    add (pct ("fg_click", "CLICK", 0.5f).next().engineGroup());
    jassert ((int) p.size() == kParamCount);
    return p;
}
}

void ForgeEngine::bind (const std::vector<ParamSpec>& specs, bool pitchedLane)
{
    pitched = pitchedLane;
    auto at = [&specs] (const char* id) { for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == id) return (int) i; return -1; };
    ids.ch = at ("fg_char"); ids.tone = at ("fg_tone"); ids.punch = at ("fg_punch"); ids.ptime = at ("fg_ptime"); ids.click = at ("fg_click");
    ids.drive = at ("fg_drive"); ids.tune = at ("fg_tune"); ids.impact = at ("fg_impact"); ids.body = at ("fg_body"); ids.ground = at ("fg_ground");
    ids.pos = at ("fg_pos"); ids.glen = at ("fg_glen"); ids.low = at ("fg_low"); ids.hang = at ("fg_hang"); ids.curve = at ("fg_curve");
    ids.hold = at ("fg_hold"); ids.hdec = at ("fg_hdec"); ids.phase = at ("fg_phase"); ids.flip = at ("fg_flip"); ids.ilen = at ("fg_ilen");
    ids.ipitch = at ("fg_ipitch"); ids.icut = at ("fg_icut"); ids.iwidth = at ("fg_iwidth"); ids.vary = at ("fg_vary"); ids.solo = at ("fg_solo");
    ids.gdark = at ("fg_gdark"); ids.gduck = at ("fg_gduck"); ids.grec = at ("fg_grec"); ids.gwidth = at ("fg_gwidth");
}

void ForgeEngine::prepare (double rate, int)
{
    sr = rate; osr = 2.0 * rate;
    dec.set (HalfbandDesign::forStage (rate, 0));
    decS.set (HalfbandDesign::forStage (rate, 0));
    ground.g.prepare (osr);
    cal = cal::at (cal::kick, 7);           // (as KICK's Forge)
    reset();
}

void ForgeEngine::reset()
{
    for (auto& v : voices) v.active = false;
    numPending = 0; sideLive = 0;
    dec.reset(); decS.reset();
    ground.g.reset();
}

bool ForgeEngine::sounding() const
{
    if (ground.g.awake() || numPending > 0) return true;
    for (auto& v : voices) if (v.active) return true;
    return false;
}

void ForgeEngine::begin (const Ctx& c, float decayKnob, const SampleData* s, int shift)
{
    auto v = [&c] (int i, float d) { return i >= 0 ? c.v (i) : d; };
    k.ch = v (ids.ch, 0.3f); k.tone = v (ids.tone, 0.5f); k.punch = v (ids.punch, 0.5f); k.ptime = v (ids.ptime, 0.5f); k.decay = clamp01 (decayKnob);
    k.click = v (ids.click, 0.5f); k.drive = v (ids.drive, 0.2f); k.impact = v (ids.impact, 0.5f); k.body = v (ids.body, 0.5f); k.ground = v (ids.ground, 0.5f);
    k.pos = v (ids.pos, 0.1f); k.glen = v (ids.glen, 0.5f); k.low = v (ids.low, 0.0f); k.hang = v (ids.hang, 0.0f); k.curve = v (ids.curve, 0.5f);
    k.hold = v (ids.hold, 0.0f); k.hdec = v (ids.hdec, 0.5f); k.phase = v (ids.phase, 0.0f); k.flip = v (ids.flip, 0.0f) > 0.5f;
    k.ilen = v (ids.ilen, 0.5f); k.ipitch = v (ids.ipitch, 0.0f); k.icut = v (ids.icut, 0.5f); k.iwidth = v (ids.iwidth, 0.5f); k.vary = v (ids.vary, 0.5f);
    k.solo = v (ids.solo, 0.0f); k.gdark = v (ids.gdark, 0.5f); k.gduck = v (ids.gduck, 0.5f); k.grec = v (ids.grec, 0.5f); k.gwidth = v (ids.gwidth, 0.0f);
    tune = v (ids.tune, 0.0f); bend = c.bend; keyShift = shift; solo = k.solo > 0.5f;
    smp = s;
    if (s != nullptr) { whole = SampleRegion(); whole.start = 0.0; whole.end = (double) s->length(); }
    // a ground that is ringing, and every hit that is sounding, follow the knobs as they stand (as on KICK)
    ground.follow (k);
    ground.shade (k);
    for (auto& w : voices) if (w.active) forge::follow (k, w, forge::forgeAt (w.fgCh));
    for (auto& w : voices) w.startedAt = -1;
}

void ForgeEngine::noteOn (int offset, float pitch, float vel, float gain, int gen, int part)
{
    if (numPending >= (int) pending.size()) return;
    pending[(size_t) numPending++] = { offset, pitch, vel, gain, gen, part };
}

void ForgeEngine::start (const Pending& p, int at)
{
    // what is ringing goes over 3 ms (a quiet echo plays over it), but not what was struck at this same moment
    if (p.gen == 0 || p.gain >= 0.5f)
        for (auto& w : voices) if (w.active && w.fadeStep == 0.0f && w.startedAt != at) w.fadeStep = 1.0f / (0.003f * (float) osr);
    int idx = -1;
    for (int i = 0; i < kVoices; ++i) if (! voices[(size_t) i].active) { idx = i; break; }
    if (idx < 0) { float lo = 1.0e9f; for (int i = 0; i < kVoices; ++i) { const auto& w = voices[(size_t) i]; if (w.fadeStep > 0.0f && w.fade * w.level() * w.mix < lo) { lo = w.fade * w.level() * w.mix; idx = i; } } }
    if (idx < 0) { float lo = 1.0e9f; for (int i = 0; i < kVoices; ++i) if (voices[(size_t) i].level() * voices[(size_t) i].mix < lo) { lo = voices[(size_t) i].level() * voices[(size_t) i].mix; idx = i; } }
    if (idx < 0) idx = 0;
    auto& v = voices[(size_t) idx];
    v = Voice();
    v.active = true; v.part = p.part; v.startedAt = at;
    v.vel = p.vel; v.mix = p.gain;
    // the note it lands on: the note played (a lane that plays notes), else the kick's resting note, G1
    const float st = tune + bend;
    const float hz = pitched ? 440.0f * std::pow (2.0f, (p.pitch + (float) keyShift - 69.0f) / 12.0f) * semis (st) : 49.0f * semis (st);
    v.f1 = juce::jlimit (16.0f, (float) (0.2 * osr), hz);
    forge::start (k, v, p.gen, smp, smp != nullptr ? &whole : nullptr, p.part == 0, rng, osr, ground);
}

float ForgeEngine::tick (Voice& v, float& side)
{
    const float t = (float) v.t;
    float y = forge::voice (v, t, smp, side, groundIn, rng, osr);
    if (forge::over (v, t)) v.active = false;
    if (v.fadeStep > 0.0f) { v.fade -= v.fadeStep; if (v.fade <= 0.0f) { v.active = false; side = 0.0f; return 0.0f; } y *= v.fade; side *= v.fade; }
    return y * cal * v.mix;
}

void ForgeEngine::render (float* L, float* R, int n, float layerGain)
{
    std::stable_sort (pending.begin(), pending.begin() + numPending, [] (const Pending& a, const Pending& b) { return a.offset < b.offset; });
    int e = 0;
    for (int i = 0; i < n; ++i)
    {
        while (e < numPending && pending[(size_t) e].offset <= i) start (pending[(size_t) e++], i);
        float y[2], s[2];
        for (int o = 0; o < 2; ++o)
        {
            float acc = 0.0f, sacc = 0.0f;
            groundIn = 0.0f;
            for (auto& v : voices)
                if (v.active)
                {
                    float sd = 0.0f;
                    const float out = tick (v, sd);
                    const float g = v.part == 1 ? layerGain : 1.0f;
                    acc += out * g; sacc += sd * cal * v.mix * g;
                }
            if (ground.g.awake()) { float gs; const float g = ground.g.tick (groundIn, gs); if (! solo) { acc += g; sacc += gs; } }
            y[o] = acc; s[o] = sacc;
        }
        const float m = dec.down (y[0], y[1]);
        float sd = 0.0f;
        if (s[0] != 0.0f || s[1] != 0.0f) sideLive = 4096;
        if (sideLive > 0) { --sideLive; sd = decS.down (s[0], s[1]); }
        L[i] += m + sd; R[i] += m - sd;
    }
    while (e < numPending) start (pending[(size_t) e++], n > 0 ? n - 1 : 0);
    numPending = 0;
}
} // namespace sf
