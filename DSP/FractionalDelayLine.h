#pragma once

#include <cstddef>
#include <vector>

/**
 * Lock-free circular delay line with 1st-order Thiran allpass fractional read.
 * All memory is allocated in prepare(); process paths are allocation-free.
 */
class FractionalDelayLine
{
public:
    static constexpr float kMinStableDelaySamples = 1.6f;

    FractionalDelayLine() = default;

    void prepare(int maxDelaySamples);
    void reset() noexcept;

    void writeSample(float sample) noexcept;
    float readSample(float delayInSamples) noexcept;

private:
    static int nextPowerOfTwo(int value) noexcept;

    std::vector<float> buffer_;
    int writeIndex_ = 0;
    int bufferMask_ = 0;
    float apState_ = 0.0f; // stores y[n-1], the previous allpass output
};
