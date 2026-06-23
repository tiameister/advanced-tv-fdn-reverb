#pragma once

#include "AdvancedFDN.h"

#include <array>

/**
 * JUCE-free facade for the TV-FDN engine.
 * Wraps AdvancedFDN<16> with a simple stereo processBlock API.
 */
class TVFDNEngine
{
public:
    TVFDNEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void processBlock(float* left, float* right, int numSamples) noexcept;
    void processBlock(const float* left, const float* right, float* outLeft, float* outRight, int numSamples) noexcept;

    void setFeedback(float feedback) noexcept;
    void setModDepth(float depthSamples) noexcept;
    void setDryWet(float mix) noexcept;
    void setModRates(const std::array<float, 16>& ratesHz) noexcept;

    /** Frequency-dependent decay EQ — delegates to AdvancedFDN::setDecayEQ. */
    void setDecayEQ(float lowFreq,  float lowT60,
                    float midFreq,  float midT60,
                    float highFreq, float highT60) noexcept;

    AdvancedFDN<16>& getFDN() noexcept { return fdn_; }
    const AdvancedFDN<16>& getFDN() const noexcept { return fdn_; }

private:
    AdvancedFDN<16> fdn_;
};
