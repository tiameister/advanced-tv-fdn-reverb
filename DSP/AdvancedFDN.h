#pragma once

#include "FractionalDelayLine.h"
#include "OrthogonalMatrix.h"
#include "WavetableLFO.h"

#include <array>
#include <cmath>
#include <vector>

/**
 * Time-Varying Feedback Delay Network (TV-FDN).
 *
 * N-channel diagonal delay lines, orthogonal FWHT mixing, multi-phase LFO
 * modulation, and true-stereo L/R injection into even/odd channels. All buffers are
 * pre-allocated in prepare().
 */
template <int NumChannels = 16>
class AdvancedFDN
{
public:
    static_assert(dsp::isPowerOfTwo(static_cast<std::size_t>(NumChannels)),
                  "AdvancedFDN requires a power-of-two channel count.");

    AdvancedFDN() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void processBlock(const float* left, const float* right, float* outLeft, float* outRight, int numSamples) noexcept;

    void setFeedback(float feedback) noexcept;
    void setModDepth(float depthSamples) noexcept;
    void setDryWet(float mix) noexcept;
    void setModRates(const std::array<float, NumChannels>& ratesHz) noexcept;

    const std::array<int, NumChannels>& getBaseDelaySamples() const noexcept { return baseDelaySamples_; }

private:
    static constexpr float kMinDelayMs       = 3.0f;
    static constexpr float kMaxDelayMs       = 18.0f;
    static constexpr float kDefaultModDepth  = 0.75f;
    static constexpr float kMaxModDepth      = 2.0f;  // absolute cap; setModDepth clamps to this
    static constexpr float kDcBlockerCutoffHz = 5.0f;

    static int nearestAvailablePrime(int target, const std::vector<bool>& isPrimeTable, std::array<bool, 4096>& used) noexcept;
    static std::vector<bool> buildPrimeSieve(int maxValue);
    static std::array<int, NumChannels> computePrimeDelaySamples(double sampleRate);

    void updateSmoothedParameters() noexcept;
    void precomputeLfoBlock(int numSamples) noexcept;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 0;

    std::array<FractionalDelayLine, NumChannels> delayLines_;
    std::array<WavetableLFO, NumChannels> lfos_;
    std::array<int, NumChannels> baseDelaySamples_{};

    std::array<float, NumChannels> hpState_{};
    std::array<float, NumChannels> lfoBlockStart_{};
    std::array<float, NumChannels> lfoBlockStep_{};

    std::array<float, NumChannels> delayed_{};
    std::array<float, NumChannels> mixed_{};

    float feedbackTarget_ = 0.85f;
    float feedbackCurrent_ = 0.85f;
    float modDepthTarget_ = kDefaultModDepth;
    float modDepthCurrent_ = kDefaultModDepth;
    float dryWetTarget_ = 1.0f;
    float dryWetCurrent_ = 1.0f;

    float paramSmoothingCoeff_ = 0.001f;
    float hpCoeff_ = 0.0f;
    float injectionNorm_ = 0.25f;
    int maxDelaySamples_ = 0;
    bool lfoBlockPrecomputed_ = false;
    bool prepared_ = false;
};

#include "AdvancedFDN.inl"
