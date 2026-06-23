#pragma once

/**
 * Single biquad section — Transposed Direct Form II (TDF-II).
 *
 * TDF-II topology
 * ───────────────
 * TDF-II is preferred over Direct Form I for floating-point implementations
 * because it reduces the number of delay elements from 4 to 2 and exhibits
 * better numerical properties at low frequencies (no large intermediate sums).
 *
 * Difference equations (two state variables s1, s2):
 *
 *   y[n]  = b0·x[n] + s1[n-1]
 *   s1[n] = b1·x[n] - a1·y[n] + s2[n-1]
 *   s2[n] = b2·x[n] - a2·y[n]
 *
 * Note: a1/a2 here are the un-negated denominator coefficients from the
 * standard z-transform convention, so the code matches the RBJ Cookbook
 * output directly after dividing by a0.
 *
 * Per-sample coefficient smoothing
 * ─────────────────────────────────
 * Coefficients are never applied instantaneously — each has a target
 * (set by setCoefficients) and a current value advanced by a one-pole
 * lowpass every sample inside processSample(). This prevents zipper noise
 * when AbsorptionBank::updateCoefficients() is called from the UI thread
 * or at block rate while the audio thread is running.
 *
 * Real-time constraints
 * ─────────────────────
 *   • setCoefficients() writes five floats atomically-enough for the
 *     one-pole smoother: a stale read at most defers the update by one sample.
 *   • processSample() performs only multiply-add operations — no branches,
 *     no transcendental math, no memory allocation.
 *   • All state is held in-place (no heap).
 */
class BiquadFilter
{
public:
    BiquadFilter() = default;

    /**
     * Initialise smoothing coefficient and clear state.
     * Must be called before processSample().
     * @param sampleRate  Host sample rate (Hz).
     */
    void prepare(double sampleRate) noexcept;

    /** Clear delay-line state without touching coefficient targets. */
    void reset() noexcept;

    /**
     * Set new coefficient targets (RBJ convention, pre-divided by a0).
     * The running values glide toward these over ~smoothingTimeMs.
     * Safe to call from any thread — worst case is a 1-sample stale read.
     */
    void setCoefficients(float b0, float b1, float b2,
                         float a1, float a2) noexcept;

    /**
     * Immediately snap running coefficient values to their current targets,
     * bypassing the one-pole smoother. Call after the very first
     * setCoefficients() during prepare() so there is no initial glide from
     * the identity filter to the EQ curve on the first audio block.
     */
    void snapCoefficientsToTargets() noexcept;

    /**
     * Process one sample.
     * Advances the one-pole smoother for every coefficient, then evaluates
     * the TDF-II difference equations.  No math beyond MAC operations.
     */
    float processSample(float x) noexcept;

private:
    // Smoothing
    static constexpr float kSmoothingTimeMs = 20.0f; // ~20 ms glide to new coefficients

    float smCoeff_ = 0.0f; // one-pole coefficient computed in prepare()

    // Coefficient targets (written by setCoefficients, UI/block-rate thread)
    float b0t_ = 1.0f, b1t_ = 0.0f, b2t_ = 0.0f;
    float a1t_ = 0.0f, a2t_ = 0.0f;

    // Running (smoothed) values — advanced per sample in processSample()
    float b0c_ = 1.0f, b1c_ = 0.0f, b2c_ = 0.0f;
    float a1c_ = 0.0f, a2c_ = 0.0f;

    // TDF-II state
    float s1_ = 0.0f;
    float s2_ = 0.0f;
};
