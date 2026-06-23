#include "EarlyReflections.h"

#include <algorithm>

int EarlyReflections::nextPowerOfTwo(int v) noexcept
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

void EarlyReflections::prepare(double        sampleRate,
                                int           maxBlockSize,
                                float         erLengthMs,
                                float         targetDensityHz,
                                float         minSpacingMs,
                                std::uint32_t seedL,
                                std::uint32_t seedR)
{
    const float clampedLengthMs = std::clamp(erLengthMs, 1.0f, static_cast<float>(kMaxErLengthMs));

    // Ring buffer must hold at least (erLength + maxBlock) samples so the
    // deepest tap is always valid when filling a complete block.
    const int erLengthSamples = static_cast<int>(
        clampedLengthMs * static_cast<float>(sampleRate) * 0.001f) + 1;

    ringCapacity_ = nextPowerOfTwo(erLengthSamples + std::max(1, maxBlockSize) + 2);
    ringMask_     = ringCapacity_ - 1;

    ringL_.assign(static_cast<std::size_t>(ringCapacity_), 0.0f);
    ringR_.assign(static_cast<std::size_t>(ringCapacity_), 0.0f);
    writeIndexL_ = 0;
    writeIndexR_ = 0;

    genL_.prepare(sampleRate, clampedLengthMs, targetDensityHz, minSpacingMs, seedL);
    genR_.prepare(sampleRate, clampedLengthMs, targetDensityHz, minSpacingMs, seedR);

    normGainL_ = genL_.normalisationGain();
    normGainR_ = genR_.normalisationGain();

    prepared_ = true;
    reset();
}

void EarlyReflections::reset() noexcept
{
    std::fill(ringL_.begin(), ringL_.end(), 0.0f);
    std::fill(ringR_.begin(), ringR_.end(), 0.0f);
    writeIndexL_ = 0;
    writeIndexR_ = 0;
}

void EarlyReflections::processBlock(const float* inLeft,
                                    const float* inRight,
                                    float*       erOutLeft,
                                    float*       erOutRight,
                                    int          numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    const int   cap     = ringCapacity_;
    const int   mask    = ringMask_;
    const float gainL   = normGainL_ * erMixGain_;
    const float gainR   = normGainR_ * erMixGain_;
    const int   numTapL = genL_.numTaps();
    const int   numTapR = genR_.numTaps();

    for (int n = 0; n < numSamples; ++n)
    {
        ringL_[static_cast<std::size_t>(writeIndexL_)] = inLeft[n];
        ringR_[static_cast<std::size_t>(writeIndexR_)] = inRight[n];

        // Left ER — convolve inputL with left velvet sequence.
        // +cap before masking guarantees a positive operand for &, preventing
        // UB from bitwise AND on negative signed integers.
        float sumL = 0.0f;
        for (int t = 0; t < numTapL; ++t)
        {
            const auto& tap     = genL_.tap(t);
            const int   readIdx = (writeIndexL_ - tap.delaySamples + cap) & mask;
            sumL += tap.sign * ringL_[static_cast<std::size_t>(readIdx)];
        }

        // Right ER — convolve inputR with right velvet sequence.
        float sumR = 0.0f;
        for (int t = 0; t < numTapR; ++t)
        {
            const auto& tap     = genR_.tap(t);
            const int   readIdx = (writeIndexR_ - tap.delaySamples + cap) & mask;
            sumR += tap.sign * ringR_[static_cast<std::size_t>(readIdx)];
        }

        erOutLeft[n]  = sumL * gainL;
        erOutRight[n] = sumR * gainR;

        // Wrap with mask — prevents signed integer overflow (~12 hr @ 48 kHz)
        writeIndexL_ = (writeIndexL_ + 1) & mask;
        writeIndexR_ = (writeIndexR_ + 1) & mask;
    }
}
