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
    void processBlock(const float* left, const float* right,
                      float* outLeft, float* outRight,
                      int numSamples) noexcept;

    // ── Unified reverb time (primary length control) ────────────────────────
    /** Set perceived reverb time [0.1, 20 s]. Drives feedback + T60 bands. */
    void setReverbTime(float reverbTimeSec) noexcept;

    /** Set spectral-tilt multipliers relative to reverbTime. */
    void setDecayShape(float bassDecayMult, float midDecayMult, float hfDecayMult) noexcept;

    // ── Room size ────────────────────────────────────────────────────────────
    /** Scale delay topology (0 = small room, 0.33 = medium, 1 = cathedral). */
    void setSize(float size) noexcept;

    // ── Modulation & output ──────────────────────────────────────────────────
    void setModDepth   (float depthSamples) noexcept;
    void setDryWet     (float mix)          noexcept;
    void setStereoWidth(float width)        noexcept;
    void setModRates   (const std::array<float, 16>& ratesHz) noexcept;

    AdvancedFDN<16>&       getFDN()       noexcept { return fdn_; }
    const AdvancedFDN<16>& getFDN() const noexcept { return fdn_; }

private:
    AdvancedFDN<16> fdn_;
};
