// FINISH (SF-000: this module is SF-000's own, not the suite's)
#include "Kit.h"
#include "../core/Loudness.h"

namespace sf
{
// =====================================================================================  FINISH
// A master stage that goes before the limiter: what a mix-bus finisher does to a whole mix, on
// one knob. Three bands (under 150 Hz, between, over 2.5 kHz), each held by its own compressor
// and given a little of its own third harmonic; a slight tone; and the sides brought up where
// they are bright. No lookahead and no delay: the limiter after it has those.
//
// What each part does at AMOUNT 100 was set by measuring a finisher at work on a test signal
// (tools/master_probe.py: sweeps and noise at stepped levels, tone steps, sides alone), until
// this stage, measured the same way, gave the same figures:
//   the middle band: held from far down and gently (1.28 to 1 over -28 dBFS, by the band's
//     mean level, with a knee 16 dB wide), slow to take hold (a quarter of it in 3 ms, the rest
//     in 80) and 250 ms to let go;
//   the low band: all but left alone (a twelfth of what its peaks are over -21 dBFS), and slow
//     to let go (a quarter in 100 ms, the rest in 3.5 s), so that it rides a kick, not pumps to it;
//   the high band: only when it is loud (1.9 to 1 over -13 dBFS), and fast (3 ms, 40 ms);
//   each band: its own odd harmonics, the third 39 dB under a tone at -10 dBFS (33 in the low
//     band) and falling twice as fast as the tone does; in each band apart, so the bass does
//     not mark the highs;
//   tone: 1 dB more in the middle band, 1.6 dB less in the high one;
//   the sides: held as the middle is (it is the middle's level that is listened to), and
//     0.2 dB up in the middle band, 3 dB up in the high one (the test signal says 3.4; a set
//     of Wiley's through the finisher, 2.1 to 2.5).
// It gives back what it takes from a full mix, and no more: 1.75 dB at all of it (it took 1.6
// LU from SF-000's own rig as it opens and 1.9 from that set of Wiley's, all but the same from
// one passage to the next). So a step of it changes the sound and not the level, and the
// limiter's own trim after it has nothing to follow. What is too quiet to be held comes up by
// that much, as with any compressor that makes up.
//
// The levels above are of a signal as the finisher was given it. AUTO puts the mix there
// whatever level it reaches this stage at: the loudest three seconds of the last minute are
// put at kRef (which is where that set of Wiley's stood when it went through the finisher),
// and taken back out after. INPUT is where AUTO starts from before it has heard anything, and
// all there is with AUTO off.
//
// At AMOUNT 0 it is not there: the samples pass untouched (it goes on listening, to be ready).
class FinishModule : public Module, public FinishSays
{
public:
    enum { AMOUNT, INPUT, AUTO };
    std::string id() const override { return "FINISH"; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("amount", "AMOUNT", 0.0f).prim().dir (1),
                 knob ("input", "INPUT", -12.0f, 24.0f, 0.0f, "dB").prim(),
                 toggle ("auto", "AUTO").prim() };
    }
    float finishLoud() const override { return dLoud.load(); }
    float finishAuto() const override { return dAuto.load(); }
    float finishHeld() const override { return redux.load(); }
    void prepare (double rate, int) override
    {
        sr = rate;
        for (auto& x : xoLo) x.set (kLowHz, sr);
        for (auto& x : xoHi) x.set (kHighHz, sr);
        for (auto& x : xoAp) x.set (kHighHz, sr);
        auto coef = [this] (float seconds) { return std::exp (-1.0f / (float) (seconds * sr)); };
        aFast = coef (0.003f); aSlow = coef (0.080f); rMid = coef (0.250f);
        aLow = coef (0.060f); rLowFast = coef (0.100f); rLowSlow = coef (3.5f);
        aHigh = coef (0.003f); rHigh = coef (0.040f);
        aAmt = coef (0.030f);
        dLow = coef (0.040f); cMid = coef (0.005f); cHigh = coef (0.001f);
        meter.prepare (rate);
        autoDb = 0.0f; gIn = -1.0f; heard = 0; fed = 0;
        for (auto& h : hist) h = -100.0f;
        histPos = 0; secMax = -100.0f; secLeft = (int) rate;
        reset();
    }
    void reset() override
    {
        for (auto& x : xoLo) x.reset();
        for (auto& x : xoHi) x.reset();
        for (auto& x : xoAp) x.reset();
        gLowFast = gLowSlow = gMidFast = gMidSlow = gHigh = 0.0f; amt = -1.0f; eLow = qMid = qHigh = 0.0f;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        float* L = io.getWritePointer (0); float* R = io.getWritePointer (1);
        // AUTO. Slowly (2 dB a second down, 1 up; in proportion over the last dB, so it settles), only while there is music
        // (over -40 LUFS), and not up until three seconds of it have been heard. A breakdown is held through, as the
        // limiter's own trim holds it: it is the loudest of the last minute that counts.
        // (Until the meter has had its three seconds, what it says is of less than that, and the first moments of a set can
        // read louder than any three seconds of it do: not taken.)
        for (int i = 0; i < c.n; ++i) meter.push (L[i], R[i]);
        const int full = (int) (3.0 * sr);
        fed = std::min (fed + c.n, full);
        const float st = fed >= full ? meter.shortTerm : -100.0f;
        secMax = std::max (secMax, st);
        if ((secLeft -= c.n) <= 0) { hist[histPos] = secMax; histPos = (histPos + 1) % kHist; secMax = -100.0f; secLeft += (int) sr; }
        float loud = secMax;
        for (const float h : hist) loud = std::max (loud, h);
        heard = st > -40.0f ? std::min (heard + c.n, full) : 0;
        const bool autoOn = c.b (AUTO);
        if (autoOn && st > -40.0f)
        {
            float step = juce::jlimit (-12.0f, 12.0f, kRef - loud - c.v (INPUT)) - autoDb;
            if (step > 0.0f && heard < full) step = 0.0f;
            autoDb += juce::jlimit (-2.0f, 1.0f, step) * (float) c.n / (float) sr;
        }
        dAuto = autoOn ? autoDb : 0.0f; dLoud = loud;
        const float in1 = dbToGain (c.v (INPUT) + (autoOn ? autoDb : 0.0f)), in0 = gIn > 0.0f ? gIn : in1;
        gIn = in1;

        const float want = c.v (AMOUNT);
        if (amt < 0.0f) amt = want;
        if (want <= 0.0f && amt <= 1.0e-4f) { amt = 0.0f; redux = 0.0f; return; }        // not there
        float most = 0.0f;
        for (int i = 0; i < c.n; ++i)
        {
            amt = want + (amt - want) * aAmt;
            const float a = amt;
            const float in = in0 + (in1 - in0) * (float) i / (float) c.n, inv = dbToGain (kMakeUp * a) / in;
            const float m = 0.5f * (L[i] + R[i]) * in, s = 0.5f * (L[i] - R[i]) * in;
            // Three bands of each, by two crossovers; the low band is also passed through the second one's two halves and put
            // together again, so that all three have been turned the same and, untouched, add up to the whole.
            float mLo, mMid, mHi, sLo, sMid, sHi, rest, p, q;
            xoLo[0].split (m, mLo, rest); xoHi[0].split (rest, mMid, mHi); xoAp[0].split (mLo, p, q); mLo = p + q;
            xoLo[1].split (s, sLo, rest); xoHi[1].split (rest, sMid, sHi); xoAp[1].split (sLo, p, q); sLo = p + q;
            // Each band's level, of the middle: the low band's by its peaks (let fall in 40 ms), the other two's by their mean
            // square (which is what makes noise and a tone of the same power come out the same, as they did when measured);
            // as the top of a tone of that power, which is what the thresholds are given as.
            eLow = std::max (std::fabs (mLo), eLow * dLow);
            qMid = mMid * mMid + (qMid - mMid * mMid) * cMid; qHigh = mHi * mHi + (qHigh - mHi * mHi) * cHigh;
            // how far each band is held, in dB: what it is over its threshold, by its ratio; taken hold of and let go in its own time
            const float lowWant = over (eLow, kLowT, 6.0f) * kLowR, midWant = over (std::sqrt (2.0f * qMid), kMidT, 16.0f) * kMidR;
            follow (gLowFast, lowWant, aLow, rLowFast); follow (gLowSlow, lowWant, aLow, rLowSlow);
            follow (gMidFast, midWant, aFast, rMid); follow (gMidSlow, midWant, aSlow, rMid);
            follow (gHigh, over (std::sqrt (2.0f * qHigh), kHighT, 6.0f) * kHighR, aHigh, rHigh);
            const float rdLow = 0.25f * gLowFast + 0.75f * gLowSlow, rdMid = 0.25f * gMidFast + 0.75f * gMidSlow;
            most = std::max (most, std::max (rdMid, std::max (rdLow, gHigh)) * a);
            // each band: its own third harmonic (a soft curve whose first bend is that), then its gain; the sides are held as the
            // middle is, and brought up besides where they are bright
            const float gl = dbToGain (-rdLow * a), gm = dbToGain ((kMidUp - rdMid) * a), gh = dbToGain ((kHighUp - gHigh) * a);
            const float mm = (shape (mLo, a, kLowCurve) * gl + shape (mMid, a, kCurve) * gm + shape (mHi, a, kCurve) * gh) * inv;
            const float ss = (shape (sLo, a, kLowCurve) * gl + shape (sMid, a, kCurve) * gm * dbToGain (kSideMid * a) + shape (sHi, a, kCurve) * gh * dbToGain (kSideHigh * a)) * inv;
            L[i] = mm + ss; R[i] = mm - ss;
        }
        redux = most;
    }
    int displayHeight() const override { return 50; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "FINISH");
        ui::levelBar (g, r.reduced (12, 20).withHeight (8), juce::jlimit (0.0f, 1.0f, redux.load() / 8.0f));
    }
private:
    // how far a level is over a threshold, in dB (nothing under it; a knee that many dB wide about it)
    static float over (float level, float threshDb, float knee)
    {
        const float d = gainToDb (level) - threshDb, h = 0.5f * knee;
        if (d <= -h) return 0.0f;
        if (d >= h) return d;
        return (d + h) * (d + h) / (2.0f * knee);
    }
    // a gain reduction following what is asked of it: up by the attack, down by the release
    static void follow (float& g, float want, float attack, float release) { g = want + (g - want) * (want > g ? attack : release); }
    // y = tanh (k x) / k: x less k squared over three x cubed to begin with. With k 1.16 that is a third harmonic 39 dB under a
    // tone at -10 dBFS; with 1.64, twice that. It never turns back, however hard it is driven (a is no more than 1).
    static float shape (float x, float a, float k) { return x + a * (std::tanh (k * x) / k - x); }

    static constexpr float kLowHz = 150.0f, kHighHz = 2500.0f;
    // each band's threshold (dBFS, of a tone's top) and how much of what is over it is taken off (1 - 1 / ratio)
    static constexpr float kLowT = -21.0f, kLowR = 0.085f, kMidT = -28.2f, kMidR = 0.22f, kHighT = -13.0f, kHighR = 0.48f;
    static constexpr float kMidUp = 1.0f, kHighUp = -1.6f, kSideMid = 0.2f, kSideHigh = 3.0f;        // dB
    static constexpr float kMakeUp = 1.75f;                                                           // dB, at all of it
    static constexpr float kCurve = 1.16f, kLowCurve = 1.64f;
    // AUTO: where the loudest three seconds of the last minute are put, LUFS
    static constexpr float kRef = -12.0f;

    double sr = 48000.0;
    Crossover xoLo[2], xoHi[2], xoAp[2];
    float aFast = 0, aSlow = 0, rMid = 0, aLow = 0, rLowFast = 0, rLowSlow = 0, aHigh = 0, rHigh = 0, aAmt = 0;
    float gLowFast = 0, gLowSlow = 0, gMidFast = 0, gMidSlow = 0, gHigh = 0, amt = -1.0f;
    float dLow = 0, cMid = 0, cHigh = 0, eLow = 0, qMid = 0, qHigh = 0;
    LoudnessMeter meter;
    static constexpr int kHist = 60;                 // AUTO's memory: the loudest short-term of each second
    float hist[kHist] {}, secMax = -100.0f, autoDb = 0.0f, gIn = -1.0f;
    int histPos = 0, secLeft = 48000, heard = 0, fed = 0;
    std::atomic<float> redux { 0 }, dAuto { 0 }, dLoud { -100 };
};
ModulePtr makeFINISH() { return std::make_unique<FinishModule>(); }
} // namespace sf
