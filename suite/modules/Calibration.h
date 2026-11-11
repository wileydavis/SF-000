#pragma once
// Gain staging: per-model level trims (dB) so every model of a role plays at the role's target
// loudness at default settings (integrated LUFS, role pattern; measured by SFTests --calibration):
//   KICK -15   PERC -22   RUMBLE -20   BASS -18   LEAD -20   PAD -22   VOICE -20   STRIKE -20
// Switching MODEL never jumps the level, and a rig at default LEVELs is balanced with ~6 dB of
// headroom on the master before GLUE / LIMIT. Trims are applied after any drive (linear).
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sf::cal
{
// KICK: 909, 808, SINE, INDUSTRIAL, MODULAR, DUB, SAMPLE
inline constexpr float kick[7] = { -5.07f, -8.68f, -3.16f, -2.91f, -2.67f, -4.76f, 0.0f };
// PERC: HAT 909, HAT 808, HAT 606, RIDE 909, CRASH, CLAP, RIM, COWBELL, CLAVE, SHAKER, METAL FM, MODAL, SAMPLE,
//       SNARE 808, SNARE 909, TOM 808, TOM 909
inline constexpr float perc[17] = { 4.11f, 0.45f, 0.13f, -6.83f, -6.77f, -1.64f, 2.09f, -1.89f, -5.45f, -3.18f, -11.87f, 8.45f, 0.0f, -4.57f, -3.57f, 0.0f, 0.0f };
// RUMBLE: REVERB, DELAY, SUB DRONE, DISTORTED, DUB BOOM, SAMPLE
inline constexpr float rumble[6] = { -2.92f, -0.35f, -1.85f, -0.32f, -3.51f, 0.0f };
// BASS: LADDER, MONO 101, SUB OSC, ACID 303, FM, SINE SUB, OFFBEAT, SAMPLE, FM 4-OP
inline constexpr float bass[9] = { 3.22f, 7.31f, -1.94f, 12.36f, 3.87f, 3.74f, 9.26f, 0.0f, 1.42f };
// LEAD: ACID 303, MONO 101, MS20, STAB DUB, STAB DETROIT, ORGAN M1, HOOVER, FM BELL, SYNC, SAMPLE, FM 4-OP
inline constexpr float lead[11] = { 11.88f, 6.22f, 0.41f, 3.17f, 2.23f, 6.23f, 2.84f, -2.38f, -1.11f, 0.0f, 1.06f };
// PAD: STRING MACHINE, POLY PAD, WAVETABLE, GRANULAR, DUB WASH, TAPE LOOP, NOISE BED, SAMPLE, FM 4-OP
inline constexpr float pad[9] = { -3.86f, -8.23f, -4.27f, 3.50f, -0.54f, -3.54f, -7.62f, 0.0f, 4.97f };
// VOICE: formant choir (no phrase loaded); phrase models relative to SAMPLE
inline constexpr float voiceChoir = -8.78f;
inline constexpr float voice[6] = { 0.00f, -1.57f, -27.18f, -21.37f, -26.81f, -4.14f };   // SAMPLE, RADIO, VOCODER, ROBOT, CHOIR, STUTTER
// STRIKE: BELL, PLATE, BAR, TUBE, MEMBRANE, GONG
inline constexpr float strike[6] = { -1.35f, 2.81f, 4.19f, 3.73f, 5.48f, -2.91f };

inline float gain (float db) { return std::pow (10.0f, db * 0.05f); }
template <size_t N> inline float at (const float (&t)[N], int i) { return gain (t[(size_t) std::max (0, std::min ((int) N - 1, i))]); }
} // namespace sf::cal
