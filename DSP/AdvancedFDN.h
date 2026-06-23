#pragma once

#include "AbsorptionBank.h"
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
 * modulation, true-stereo injection, and M/S stereo width control.
 * All buffers are pre-allocated in prepare().
 *
 * Output tap topology (post-FWHT)
 * ────────────────────────────────
 * The audible output is tapped from mixed_[i] (post-FWHT) rather than from
 * delayed_[i] (pre-FWHT). This means every output sample is a diffuse linear
 * combination of ALL 16 delay channels — not just 8 — producing the enveloping,
 * wrap-around spatial field characteristic of professional reverbs.
 *
 * Level analysis: for uncorrelated channels each of RMS A,
 *   mixed_[i] after FWHT normalisation ≈ A  (energy-preserving)
 *   tap = mixed_[i] * norm  ≈  delayed_[i] * norm  (same output level)
 * so the change is perceptually louder in spatial diffusion, not in peak level.
 *
 * Stereo width (M/S)
 * ──────────────────
 * After accumulating wetLeft/wetRight, a Mid/Side matrix scales the stereo
 * difference signal:
 *   mid  = (L + R) / 2
 *   side = (L - R) / 2 × width
 *   L_out = mid + side,  R_out = mid − side
 * At width=1: identity.  At width=0: mono.  At width>1: hyper-wide.
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

    /**
     * Set stereo width of the FDN output via an M/S matrix.
     *
     * @param width  0 = mono, 1 = natural (default), 2 = hyper-wide.
     *               Clamped to [0, 2]. Smoothed per-sample to prevent clicks.
     */
    void setStereoWidth(float width) noexcept;

    /**
     * Set frequency-dependent decay parameters.
     *
     * Internally converts each band's T60 target to a per-loop gain for every
     * FDN channel (using that channel's delay time), then pushes new targets
     * to the biquad coefficient smoothers. Safe to call from the UI thread or
     * at block rate — the audio thread only reads the smoothed coefficients.
     *
     * @param lowFreq   Low-shelf corner frequency (Hz).   e.g. 250 Hz
     * @param lowT60    Target T60 for bass frequencies (s). e.g. 3.0 s (slow decay)
     * @param midFreq   Peak/bell centre frequency (Hz).   e.g. 1500 Hz
     * @param midT60    Target T60 for mids (s).           e.g. 2.0 s
     * @param highFreq  High-shelf corner frequency (Hz).  e.g. 5000 Hz
     * @param highT60   Target T60 for HF (s).             e.g. 0.8 s (fast decay)
     */
    void setDecayEQ(float lowFreq,  float lowT60,
                    float midFreq,  float midT60,
                    float highFreq, float highT60) noexcept;

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

    /**
     * Recompute AbsorptionBank coefficients for all channels.
     * Called from prepare() and setDecayEQ(). NOT called per-sample.
     * Uses std::pow/sin/cos via AbsorptionBank — keep off the hot path.
     */
    void updateFilterCoefficients() noexcept;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 0;

    std::array<FractionalDelayLine, NumChannels> delayLines_;
    std::array<WavetableLFO, NumChannels>        lfos_;
    std::array<AbsorptionBank, NumChannels>      absorptionBanks_;
    std::array<int, NumChannels>                 baseDelaySamples_{};

    std::array<float, NumChannels> hpState_{};
    std::array<float, NumChannels> lfoBlockStart_{};
    std::array<float, NumChannels> lfoBlockStep_{};

    std::array<float, NumChannels> delayed_{};
    std::array<float, NumChannels> mixed_{};

    float feedbackTarget_      = 0.85f;
    float feedbackCurrent_     = 0.85f;
    float modDepthTarget_      = kDefaultModDepth;
    float modDepthCurrent_     = kDefaultModDepth;
    float dryWetTarget_        = 1.0f;
    float dryWetCurrent_       = 1.0f;
    float stereoWidthTarget_   = 1.0f;  // 0=mono  1=natural  2=hyper-wide
    float stereoWidthCurrent_  = 1.0f;

    float paramSmoothingCoeff_ = 0.001f;
    float hpCoeff_             = 0.0f;
    float injectionNorm_       = 0.25f;
    int   maxDelaySamples_     = 0;
    bool  lfoBlockPrecomputed_ = false;
    bool  prepared_            = false;

    // ── Decay EQ parameters (written by setDecayEQ, read by updateFilterCoefficients) ──
    float decayLowFreq_   = 250.0f;   // Low-shelf corner (Hz)
    float decayLowT60_    = 3.0f;     // Bass T60 (s) — slow, warm decay
    float decayMidFreq_   = 1500.0f;  // Mid peak centre (Hz)
    float decayMidT60_    = 2.0f;     // Mid T60 (s)
    float decayMidQ_      = 0.707f;   // Mid peak Q (Butterworth default)
    float decayHighFreq_  = 5000.0f;  // High-shelf corner (Hz)
    float decayHighT60_   = 0.8f;     // HF T60 (s) — fast air absorption
};

#include "AdvancedFDN.inl"
