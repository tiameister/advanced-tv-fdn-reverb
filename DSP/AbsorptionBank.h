#pragma once

#include "BiquadFilter.h"

/**
 * Three-band absorption filter bank for frequency-dependent decay (FDD).
 *
 * Purpose
 * ───────
 * Inserted once per channel inside the TV-FDN feedback loop. Each channel
 * runs its own AbsorptionBank so the T60 can be sculpted independently per
 * delay line. A low shelf, a peak/bell, and a high shelf are cascaded in that
 * order to model:
 *
 *   Low Shelf  → controls bass decay time  (e.g., woolly vs. tight)
 *   Peak/Bell  → controls mid decay time   (presence shaping)
 *   High Shelf → controls HF decay time    (air / damping — always fastest)
 *
 * T60 → loop gain formula
 * ────────────────────────
 * For a delay line of length D (seconds) inside a feedback network, the
 * per-loop attenuation required to achieve a target T60 is:
 *
 *   linearGain = 10^(-3 · D / T60)            ... decays by –60 dB in T60 s
 *
 * This gain is applied as the peak/shelf amplitude of each biquad filter at
 * its center/corner frequency. Hard-clamped to 0.999 — a gain ≥ 1 in a
 * feedback loop causes exponential divergence to NaN.
 *
 * RBJ Cookbook filter types used
 * ───────────────────────────────
 *   Low Shelf  : "lowShelf"  (S = 1, unity slope)
 *   Peak/Bell  : "peakingEQ" (parametric Q)
 *   High Shelf : "highShelf" (S = 1, unity slope)
 *
 * All coefficient math (sin, cos, sqrt, pow) lives EXCLUSIVELY in
 * updateCoefficients(), which must be called from the UI thread or at the
 * start of each audio block — NEVER inside the per-sample loop.
 *
 * Real-time constraints
 * ─────────────────────
 *   • processSample() calls BiquadFilter::processSample() three times — only
 *     MAC operations, fully allocation-free.
 *   • updateCoefficients() uses sin/cos/sqrt/pow but is off the hot path.
 *   • All state pre-allocated; no heap use after prepare().
 */
class AbsorptionBank
{
public:
    AbsorptionBank() = default;

    /**
     * Prepare all three biquad sections.
     * Must be called before updateCoefficients() or processSample().
     * @param sampleRate  Host sample rate (Hz).
     */
    void prepare(double sampleRate) noexcept;

    /** Clear biquad delay-line states without touching coefficient targets. */
    void reset() noexcept;

    /**
     * Snap all three biquad running coefficients to their current targets.
     * Call once after updateCoefficients() during prepare() to prevent a
     * ~20 ms glide from unity to the EQ curve on the very first audio block.
     */
    void snapCoefficientsToTargets() noexcept;

    /**
     * Recalculate biquad coefficients from perceptual parameters.
     *
     * Designed to be called at block rate from the audio thread preamble,
     * or from the UI/parameter thread — NOT per-sample.
     * Uses std::sin, std::cos, std::sqrt, std::pow internally.
     *
     * @param sampleRate          Host sample rate (Hz).
     * @param channelDelayTimeSec Delay-line round-trip time (D, in seconds).
     *                            Determines per-loop attenuation for the target T60.
     * @param lowShelfFreqHz      Corner frequency of the low-shelf filter (Hz).
     * @param t60LowSec           Target T60 for bass frequencies (seconds).
     * @param peakFreqHz          Centre frequency of the mid peak/notch (Hz).
     * @param t60MidSec           Target T60 for mid frequencies (seconds).
     * @param peakQ               Q-factor of the mid peak/bell filter (typical: 0.7–2.0).
     * @param highShelfFreqHz     Corner frequency of the high-shelf filter (Hz).
     * @param t60HighSec          Target T60 for high frequencies (seconds).
     */
    void updateCoefficients(double sampleRate,
                            float  channelDelayTimeSec,
                            float  lowShelfFreqHz,  float t60LowSec,
                            float  peakFreqHz,      float t60MidSec,  float peakQ,
                            float  highShelfFreqHz, float t60HighSec) noexcept;

    /**
     * Process one sample through the three-biquad cascade.
     * Real-time safe: only MAC operations (delegated to BiquadFilter).
     */
    float processSample(float x) noexcept;

private:
    // Maximum gain allowed inside a feedback loop — strictly < 1 to prevent
    // exponential divergence.
    static constexpr float kMaxLoopGain = 0.999f;

    /**
     * Compute the per-loop linear gain that achieves a target T60 for a
     * delay line of length channelDelayTimeSec.
     * Result is always clamped to [0, kMaxLoopGain].
     */
    static float t60ToLinearGain(float channelDelayTimeSec,
                                 float t60Sec) noexcept;

    // Helper: compute and push low-shelf coefficients (RBJ, S = 1).
    void computeLowShelf (double sampleRate, float freqHz, float gain) noexcept;

    // Helper: compute and push peaking-EQ coefficients (RBJ).
    void computePeakEQ   (double sampleRate, float freqHz, float gain, float Q) noexcept;

    // Helper: compute and push high-shelf coefficients (RBJ, S = 1).
    void computeHighShelf(double sampleRate, float freqHz, float gain) noexcept;

    double sampleRate_ = 44100.0;

    BiquadFilter lowShelf_;
    BiquadFilter peak_;
    BiquadFilter highShelf_;
};
