#pragma once

#include "DattorroReverb.h"

#include <cmath>

/**
 * Top-level reverb engine facade wrapping DattorroReverb.
 *
 * Exposes five parameters: time, size, damping, preDelay, mix.
 * All processing is allocation-free in processBlock().
 */
class ReverbEngine
{
public:
    ReverbEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void processBlock(float* left, float* right, int numSamples) noexcept;

    void setTime(float seconds) noexcept       { dattorro_.setTime(seconds); }
    void setSize(float size01) noexcept        { dattorro_.setSize(size01); }
    void setDamping(float hz) noexcept         { dattorro_.setDamping(hz); }
    void setPreDelayMs(float ms) noexcept      { dattorro_.setPreDelayMs(ms); }
    void setMix(float wet01) noexcept          { dattorro_.setMix(wet01); }

    const DattorroReverb& dattorro() const noexcept { return dattorro_; }

private:
    DattorroReverb dattorro_;
    bool prepared_ = false;
};
