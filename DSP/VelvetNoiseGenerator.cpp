#include "VelvetNoiseGenerator.h"

#include <algorithm>
#include <cmath>

std::uint32_t VelvetNoiseGenerator::xorshift32(std::uint32_t& state) noexcept
{
    // Marsaglia xorshift32 — period 2^32-1, no division, no modulo
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state <<  5;
    return state;
}

float VelvetNoiseGenerator::toFloat01(std::uint32_t bits) noexcept
{
    // Map [1, 2^32-1] → [0.0, 1.0)
    return static_cast<float>(bits) * 2.3283064365386963e-10f; // 1 / 2^32
}

void VelvetNoiseGenerator::prepare(double        sampleRate,
                                   float         erLengthMs,
                                   float         targetDensityHz,
                                   float         minSpacingMs,
                                   std::uint32_t seed) noexcept
{
    numTaps_  = 0;
    normGain_ = 1.0f;
    taps_.fill({});

    // Guard against degenerate inputs
    if (sampleRate <= 0.0 || erLengthMs <= 0.0f || seed == 0u)
        seed = 0xDEADBEEFu;

    const int sequenceLength = static_cast<int>(
        std::min(static_cast<double>(erLengthMs) * sampleRate * 0.001,
                 static_cast<double>(kMaxSequenceLength)));

    if (sequenceLength < 2)
        return;

    // Number of segments = total impulses we want to place.
    // targetDensityHz impulses/sec × erLengthMs/1000 sec = total impulses.
    const int numSegments = static_cast<int>(
        std::clamp(targetDensityHz * static_cast<float>(erLengthMs) * 0.001f,
                   2.0f, static_cast<float>(kMaxTaps)));

    const float segmentLengthF = static_cast<float>(sequenceLength) / static_cast<float>(numSegments);

    // If minSpacingMs >= average segment duration the nudge logic collapses
    // every tap onto a rigid grid, destroying the RVN randomisation and causing
    // metallic comb filtering. Clamp to 75% of the average segment length in ms
    // so there is always meaningful positional freedom within each segment.
    const float avgSegmentMs      = (1000.0f / targetDensityHz);
    const float maxAllowedSpacingMs = avgSegmentMs * 0.75f;
    const float safeSpacingMs     = std::min(minSpacingMs, maxAllowedSpacingMs);

    const int   minSpacingSamples = static_cast<int>(safeSpacingMs * static_cast<float>(sampleRate) * 0.001f);

    std::uint32_t rngState = (seed == 0u) ? 1u : seed;

    int  lastTapSample = -minSpacingSamples - 1; // so first tap is always eligible

    for (int seg = 0; seg < numSegments && numTaps_ < kMaxTaps; ++seg)
    {
        const float segStart = segmentLengthF * static_cast<float>(seg);
        const float r        = toFloat01(xorshift32(rngState));

        int tapSample = static_cast<int>(segStart + r * segmentLengthF);
        tapSample     = std::clamp(tapSample, 0, sequenceLength - 1);

        // Enforce minimum spacing — nudge forward if too close to previous tap
        if (tapSample - lastTapSample < minSpacingSamples)
            tapSample = lastTapSample + minSpacingSamples;

        if (tapSample >= sequenceLength || tapSample < 0)
            continue;

        // Randomise sign independently from position
        const float sign = (xorshift32(rngState) & 1u) ? 1.0f : -1.0f;

        taps_[static_cast<std::size_t>(numTaps_)] = { tapSample, sign };
        ++numTaps_;
        lastTapSample = tapSample;
    }

    // Energy-preserving normalisation: sum of (sign)^2 = numTaps
    normGain_ = (numTaps_ > 0) ? (1.0f / std::sqrt(static_cast<float>(numTaps_))) : 1.0f;
}
