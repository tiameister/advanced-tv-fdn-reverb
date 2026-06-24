#pragma once

#include <array>
#include <vector>

/**
 * Lightweight multi-tap early reflections (sparse tap-delay network).
 *
 * 6 prime-ish taps per channel in the 5–45 ms range, exponential amplitude
 * decay vs delay, and a gentle HF shelf on the summed ER output.
 * Allocation-free in processBlock().
 */
class ErTapNetwork
{
public:
    static constexpr int kNumTaps = 6;

    ErTapNetwork() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void processBlock(const float* inLeft,
                      const float* inRight,
                      float*       outLeft,
                      float*       outRight,
                      int          numSamples) noexcept;

private:
    struct Tap
    {
        int   delaySamples = 0;
        float gain         = 0.0f;
    };

    static int nextPowerOfTwo(int v) noexcept;

    std::vector<float> ringL_;
    std::vector<float> ringR_;
    int ringCapacity_ = 0;
    int ringMask_     = 0;
    int writeIndexL_  = 0;
    int writeIndexR_  = 0;

    std::array<Tap, kNumTaps> tapsL_{};
    std::array<Tap, kNumTaps> tapsR_{};

    // One-pole LP state for gentle HF attenuation on ER sum (wall absorption).
    float shelfLpL_    = 0.0f;
    float shelfLpR_    = 0.0f;
    float shelfCoeff_  = 0.0f;
    float shelfMix_    = 0.35f;  // amount of HF removed from ER

    bool prepared_ = false;
};
