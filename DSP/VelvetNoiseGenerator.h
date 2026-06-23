#pragma once

#include <array>
#include <cstdint>

/**
 * Deterministic ternary velvet noise sequence generator.
 *
 * A velvet noise sequence is a sparse series of {+1, 0, -1} values whose
 * non-zero taps are spaced pseudo-randomly within fixed-size segments. This
 * gives a flat magnitude spectrum (no comb filtering) while remaining
 * extremely cheap to convolve — non-zero taps require only an add, never a
 * multiply by a floating-point coefficient.
 *
 * Generation algorithm (Randomised Velvet Noise, Välimäki et al.):
 *   Divide the sequence into M equal segments of length Ls = N / M.
 *   In segment k place exactly one impulse at position p_k = round(Ls * r_k),
 *   where r_k ∈ [0, 1) is a uniform random number.
 *   The sign of each impulse is independently randomised: s_k ∈ {+1, -1}.
 *
 * All computation happens in prepare(). The audio thread reads a pre-built,
 * immutable tap table — zero allocations, zero branching in the hot path.
 */
class VelvetNoiseGenerator
{
public:
    static constexpr int kMaxSequenceLength = 20000; // ≥ 100 ms @ 192 kHz
    static constexpr int kMaxTaps = 512;             // upper bound on non-zero taps

    struct Tap
    {
        int   delaySamples = 0;
        float sign         = 1.0f; // +1.0f or -1.0f
    };

    VelvetNoiseGenerator() = default;

    /**
     * Build the tap table.
     *
     * @param sampleRate          Host sample rate (Hz).
     * @param erLengthMs          Desired sequence window length in milliseconds.
     * @param targetDensityHz     Average impulse rate in impulses-per-second.
     *                            Typical range: 2000–4000 Hz for smooth ER.
     * @param minSpacingMs        Minimum distance between any two taps (prevents
     *                            perceptually fused clusters).
     * @param seed                PRNG seed. Use distinct values for L and R to
     *                            produce fully decorrelated sequences.
     */
    void prepare(double sampleRate,
                 float  erLengthMs,
                 float  targetDensityHz,
                 float  minSpacingMs,
                 std::uint32_t seed) noexcept;

    /** Number of valid taps after prepare(). */
    int  numTaps()                    const noexcept { return numTaps_; }

    /** Access the pre-built tap table (read-only, audio-thread safe). */
    const Tap& tap(int index)         const noexcept { return taps_[static_cast<std::size_t>(index)]; }

    /**
     * Normalisation gain to apply to the summed ER output so that the RMS
     * energy of the ER burst matches the input signal energy.
     * = 1 / sqrt(numTaps)
     */
    float normalisationGain()         const noexcept { return normGain_; }

private:
    /** Minimal xorshift32 PRNG — deterministic, allocation-free. */
    static std::uint32_t xorshift32(std::uint32_t& state) noexcept;

    /** Maps a uint32 to [0.0, 1.0). */
    static float toFloat01(std::uint32_t bits) noexcept;

    std::array<Tap, kMaxTaps> taps_{};
    int   numTaps_  = 0;
    float normGain_ = 1.0f;
};
