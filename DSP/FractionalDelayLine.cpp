#include "FractionalDelayLine.h"

#include <algorithm>
#include <cmath>

// ── Catmull-Rom Hermite cubic interpolation ──────────────────────────────────
// t   : fractional position in [0, 1)  (interpolates between x0 and x1)
// xm1 : sample one step older   than x0
// x0  : sample at the floored integer read position
// x1  : sample one step newer   than x0
// x2  : sample two steps newer  than x0
// Horner form for minimum multiplications.
float FractionalDelayLine::hermite4(float t, float xm1, float x0, float x1, float x2) noexcept
{
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

void FractionalDelayLine::prepare(int maxDelaySamples)
{
    // +2 guarantees the Hermite x[2] tap (two steps ahead of the integer read)
    // is always inside the allocated region even at maximum delay.
    bufferCapacity_ = nextPowerOfTwo(std::max(4, maxDelaySamples + 2));
    bufferMask_     = bufferCapacity_ - 1;
    buffer_.assign(static_cast<std::size_t>(bufferCapacity_), 0.0f);
    reset();
}

void FractionalDelayLine::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
}

void FractionalDelayLine::writeSample(float sample) noexcept
{
    buffer_[static_cast<std::size_t>(writeIndex_)] = sample;
    writeIndex_ = (writeIndex_ + 1) & bufferMask_;
}

float FractionalDelayLine::readSample(float delayInSamples) noexcept
{
    // Hermite x[2] reads writeIndex_ - delayInt + 1; must be < writeIndex_,
    // so delayInt must be >= 2.
    delayInSamples = std::max(kMinStableDelaySamples, delayInSamples);

    const int   delayInt  = static_cast<int>(delayInSamples);
    const float t         = delayInSamples - static_cast<float>(delayInt);
    const int   cap       = bufferCapacity_;
    const int   mask      = bufferMask_;

    // Base index: most recent sample in the [x0, x1] interpolation window.
    // Adding cap before masking guarantees the operand of & is non-negative
    // regardless of writeIndex_ or delayInt magnitude — no UB from casting
    // a negative signed int to std::size_t.
    const int base = writeIndex_ - delayInt - 1;

    const float xm1 = buffer_[static_cast<std::size_t>((base - 1 + cap) & mask)];
    const float x0  = buffer_[static_cast<std::size_t>((base     + cap) & mask)];
    const float x1  = buffer_[static_cast<std::size_t>((base + 1 + cap) & mask)];
    const float x2  = buffer_[static_cast<std::size_t>((base + 2 + cap) & mask)];

    return hermite4(t, xm1, x0, x1, x2);
}

int FractionalDelayLine::nextPowerOfTwo(int value) noexcept
{
    int power = 1;
    while (power < value)
        power <<= 1;
    return power;
}
