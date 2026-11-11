#pragma once
// SF-000 profile: what a snapshot sounded like, worked out from the bars kept with it. Nothing here
// is asked of the player: no name, no tag.
//   From the clip: the spectrum's shape, how much low end, how bright, how many hits a beat, tone
//   against texture, how much it changes from bar to bar, and where the hits fall in the bar.
//   From the rig itself: how much of each lane there was, how wet (the returns' share), and how
//   loud the whole of it was before the master chain evened it out.
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"

namespace sf0
{
struct Profile
{
    bool has = false;
    // each from 0 to 1
    float energy = 0.0f;                        // how much is going on: level before the master, hits, brightness
    float weight = 0.0f;                        // thin .. heavy: the low end's share
    float bright = 0.0f;                        // dark .. bright: the top's share of everything above the bass
    float density = 0.0f;                       // sparse .. busy: hits a beat
    float tone = 0.0f;                          // texture .. tone: how much of the middle is pitched
    float movement = 0.0f;                      // still .. moving: change from one bar to the next
    float wet = 0.0f;                           // dry .. wet: the returns' share of the mix
    std::array<float, 16> bands {};             // the spectrum's shape, 30 Hz to 16 kHz: each band against the loudest, over 48 dB
    std::array<float, 32> pulse {};             // where the hits fall in the bar, sixteen steps: the low end's, then the top's
    std::array<float, kLanes> lanes {};         // how much of each lane
    float bpm = 0.0f;
    int bars = 0;

    juce::var toVar() const;
    static Profile fromVar (const juce::var&);
};

Profile profileOf (const Engine::Heard&);

// How unlike two sounds are: 0 the same. colourOnly leaves their energy out of it.
float distance (const Profile& a, const Profile& b, bool colourOnly = false);

// One colour for a profile: dark sounds deep red, through amber and yellow, to near white for the brightest.
juce::Colour heat (float bright);
} // namespace sf0
