#include "FractionalDelayLine.h"

#include <algorithm>
#include <cmath>

void FractionalDelayLine::prepare(int maxDelaySamples)
{
    const int bufferSize = nextPowerOfTwo(std::max(2, maxDelaySamples + 2));
    buffer_.assign(static_cast<std::size_t>(bufferSize), 0.0f);
    bufferMask_ = bufferSize - 1;
    reset();
}

void FractionalDelayLine::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
    apState_ = 0.0f;
}

void FractionalDelayLine::writeSample(float sample) noexcept
{
    buffer_[static_cast<std::size_t>(writeIndex_)] = sample;
    writeIndex_ = (writeIndex_ + 1) & bufferMask_;
}

float FractionalDelayLine::readSample(float delayInSamples) noexcept
{
    // Thiran is unstable below 0.5 samples; readIndex+1 must stay behind writeIndex_
    // in read-before-write FDN loops, so enforce integer delay >= 1 with margin.
    delayInSamples = std::max(kMinStableDelaySamples, delayInSamples);

    const int delayInt = static_cast<int>(delayInSamples);
    const float delayFrac = delayInSamples - static_cast<float>(delayInt);
    const float alpha = (1.0f - delayFrac) / (1.0f + delayFrac);

    const int readIndex = writeIndex_ - delayInt - 1;
    const float xm1 = buffer_[static_cast<std::size_t>(readIndex) & static_cast<std::size_t>(bufferMask_)];
    const float x0 = buffer_[static_cast<std::size_t>(readIndex + 1) & static_cast<std::size_t>(bufferMask_)];

    // 1st-order Thiran allpass: y[n] = alpha * x[n-1] + x[n] - alpha * y[n-1]
    const float output = alpha * xm1 + x0 - alpha * apState_;
    apState_ = output;
    return output;
}

int FractionalDelayLine::nextPowerOfTwo(int value) noexcept
{
    int power = 1;
    while (power < value)
        power <<= 1;
    return power;
}
