#pragma once

#include "AbsorptionBank.h"
#include "FractionalDelayLine.h"
#include "OrthogonalMatrix.h"
#include "WavetableLFO.h"

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

/**
 * Time-Varying Feedback Delay Network (TV-FDN).
 *
 * Unified Reverb Time control
 * ───────────────────────────
 * setReverbTime(rt) is the primary length control. It simultaneously:
 *   1. Computes feedback = 10^(−3 × avgDelayS / rt) — the DC loop gain that
 *      achieves −60 dB at approximately rt seconds.
 *   2. Derives per-band T60 targets as multiples of rt via setDecayShape():
 *        lowT60  = rt × bassDecayMult   (default 1.4 → bass lingers)
 *        midT60  = rt × midDecayMult    (default 1.0 → flat at rt)
 *        highT60 = rt × hfDecayMult     (default 0.2 → HF dies fast)
 *   3. Feeds each AbsorptionBank a *feedback-compensated* T60:
 *        absorptionGain(f) = 10^(−3D/T60(f)) / feedback
 *      so the combined loop gain (feedback × absorption) matches the target exactly.
 *      If absorptionGain ≥ 1 (target longer than feedback alone achieves):
 *        → bank passes through unmodified (infinite effective T60 for that band).
 *
 * Room Size
 * ─────────
 * setSize(0…1) rescales the prime delay template to a new [min, max] ms range
 * without reallocating memory (buffers are sized for kAbsMaxDelayMs = 80 ms at
 * prepare time). The change is flagged and applied at the start of the next
 * processBlock, causing a brief delay-line reset (acceptable for room-mode change).
 *
 *   size=0.00 → ~[ 2,  8] ms  small room
 *   size=0.33 → ~[ 3, 18] ms  medium room (default, matches original primes)
 *   size=0.67 → ~[ 7, 40] ms  large hall
 *   size=1.00 → ~[15, 80] ms  cathedral
 *
 * Output tap topology (post-FWHT)
 * ────────────────────────────────
 * The audible output is tapped from mixed_[i] (post-FWHT diffuse field) so both
 * L and R outputs are enveloping blends of all 16 delay lines, not just 8.
 * Level is preserved: mixed_[i] × norm ≈ delayed_[i] × norm for uncorrelated
 * channels (FWHT is unitary).
 *
 * Stereo width (M/S)
 * ──────────────────
 *   mid  = (L + R) / 2,  side = (L − R) / 2 × width
 *   L_out = mid + side,  R_out = mid − side
 * At width=1: identity. At width=0: mono. At width=2: hyper-wide.
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
    void processBlock(const float* left, const float* right,
                      float* outLeft, float* outRight,
                      int numSamples) noexcept;

    // ── Unified reverb time (primary length control) ────────────────────────
    /**
     * Set the perceived reverb time.
     * Internally computes FDN feedback and all three T60 band targets.
     * @param reverbTimeSec  Target T60 in seconds [0.1, 20].
     */
    void setReverbTime(float reverbTimeSec) noexcept;

    /**
     * Set spectral-tilt multipliers applied relative to reverbTime.
     * setReverbTime() automatically re-applies the current multipliers.
     * @param bassDecayMult  lowT60  = reverbTime × bassDecayMult  [0.5, 3.0]
     * @param midDecayMult   midT60  = reverbTime × midDecayMult   [0.5, 2.0]
     * @param hfDecayMult    highT60 = reverbTime × hfDecayMult    [0.05, 1.0]
     */
    void setDecayShape(float bassDecayMult, float midDecayMult, float hfDecayMult) noexcept;

    // ── Room size ────────────────────────────────────────────────────────────
    /**
     * Scale the FDN delay topology.
     * Change is applied at the start of the next processBlock (brief reset).
     * @param size  0 = small (~2–8 ms), 0.33 = medium (default), 1 = cathedral (~15–80 ms).
     */
    void setSize(float size) noexcept;

    // ── Modulation & output ──────────────────────────────────────────────────
    void setModDepth    (float depthSamples) noexcept;
    void setDryWet      (float mix)          noexcept;
    void setStereoWidth (float width)        noexcept;
    void setModRates    (const std::array<float, NumChannels>& ratesHz) noexcept;

    const std::array<int, NumChannels>& getBaseDelaySamples() const noexcept
    { return baseDelaySamples_; }

private:
    // ── Size / delay constants ────────────────────────────────────────────────
    static constexpr float kDefaultMinDelayMs = 3.0f;
    static constexpr float kDefaultMaxDelayMs = 18.0f;
    static constexpr float kAbsMaxDelayMs     = 80.0f;  // buffer allocation ceiling
    static constexpr float kDefaultSize       = 0.33f;  // ≈ medium room [3, 18] ms

    static constexpr float kDefaultModDepth   = 0.75f;
    static constexpr float kMaxModDepth       = 2.0f;
    static constexpr float kDcBlockerCutoffHz = 5.0f;

    // ── Fixed decay-EQ corner / centre frequencies ────────────────────────────
    // Users control relative multipliers, not raw frequencies.
    static constexpr float kDecayLowFreqHz  = 250.0f;
    static constexpr float kDecayMidFreqHz  = 1500.0f;
    static constexpr float kDecayMidQ       = 0.707f;
    static constexpr float kDecayHighFreqHz = 5000.0f;

    // ── Prime delay helpers ───────────────────────────────────────────────────
    static int nearestAvailablePrime(int target,
                                     const std::vector<bool>& isPrimeTable,
                                     std::array<bool, 4096>& used) noexcept;
    static std::vector<bool> buildPrimeSieve(int maxValue);
    static std::array<int, NumChannels> computePrimeDelaySamples(
        double sampleRate,
        float  minMs = kDefaultMinDelayMs,
        float  maxMs = kDefaultMaxDelayMs);

    /** Log-linear size → [minMs, maxMs] mapping. */
    static void getSizeRange(float size, float& minMs, float& maxMs) noexcept;

    /** Scale baseDelaySamples_ from primeBaseDelaySamples_ to [minMs, maxMs]. */
    void scaleDelaysFromPrimes(float minMs, float maxMs) noexcept;

    void updateSmoothedParameters() noexcept;
    void precomputeLfoBlock(int numSamples) noexcept;

    /**
     * Recompute AbsorptionBank coefficients for all channels, applying
     * feedback-compensation so the combined loop gain matches T60 targets.
     * Uses feedbackTarget_ (not feedbackCurrent_) for coefficient stability.
     * Called from setReverbTime(), setDecayShape(), applySizePendingUpdate().
     * NOT per-sample.
     */
    void updateFilterCoefficients() noexcept;

    /** Average delay (seconds) across all channels. Used by setReverbTime(). */
    float computeAvgDelaySec() const noexcept;

    /**
     * Apply the pending size change at block start.
     * Rescales baseDelaySamples_, resets delay lines, re-derives feedback
     * and filter coefficients.  Allocation-free.
     */
    void applySizePendingUpdate() noexcept;

    // ── Engine state ──────────────────────────────────────────────────────────
    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 0;

    std::array<FractionalDelayLine, NumChannels> delayLines_;
    std::array<WavetableLFO, NumChannels>        lfos_;
    std::array<AbsorptionBank, NumChannels>      absorptionBanks_;

    std::array<int, NumChannels> baseDelaySamples_{};
    // Prime delays computed once at prepare() for the default size.
    // setSize() rescales these into baseDelaySamples_ — no re-sieve needed.
    std::array<int, NumChannels> primeBaseDelaySamples_{};

    std::array<float, NumChannels> hpState_{};
    std::array<float, NumChannels> lfoBlockStart_{};
    std::array<float, NumChannels> lfoBlockStep_{};
    std::array<float, NumChannels> delayed_{};
    std::array<float, NumChannels> mixed_{};

    // ── Unified reverb time ───────────────────────────────────────────────────
    float reverbTimeSec_   = 2.5f;
    float bassDecayMult_   = 1.4f;   // lowT60  = reverbTime × bassDecayMult
    float midDecayMult_    = 1.0f;   // midT60  = reverbTime × midDecayMult
    float hfDecayMult_     = 0.2f;   // highT60 = reverbTime × hfDecayMult

    // ── Derived per-band T60 targets (set by setReverbTime + setDecayShape) ──
    float decayLowT60_  = 3.5f;  // reverbTimeSec_ * bassDecayMult_
    float decayMidT60_  = 2.5f;  // reverbTimeSec_ * midDecayMult_
    float decayHighT60_ = 0.5f;  // reverbTimeSec_ * hfDecayMult_

    // ── FDN feedback (derived from reverbTime + avgDelay) ────────────────────
    float feedbackTarget_  = 0.85f;
    float feedbackCurrent_ = 0.85f;

    // ── Modulation ────────────────────────────────────────────────────────────
    float modDepthTarget_     = kDefaultModDepth;
    float modDepthCurrent_    = kDefaultModDepth;
    float dryWetTarget_       = 1.0f;
    float dryWetCurrent_      = 1.0f;
    float stereoWidthTarget_  = 1.0f;
    float stereoWidthCurrent_ = 1.0f;

    // ── Room size ─────────────────────────────────────────────────────────────
    float sizeTarget_ = kDefaultSize;
    std::atomic<bool> sizeNeedsUpdate_{false};

    float paramSmoothingCoeff_ = 0.001f;
    float hpCoeff_             = 0.0f;
    float injectionNorm_       = 0.25f;
    bool  lfoBlockPrecomputed_ = false;
    bool  prepared_            = false;
};

#include "AdvancedFDN.inl"
