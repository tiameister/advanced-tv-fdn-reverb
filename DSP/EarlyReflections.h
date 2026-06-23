#pragma once

#include "VelvetNoiseGenerator.h"

#include <array>
#include <cstdint>
#include <vector>

/**
 * True-stereo early reflections via two fully independent velvet noise
 * sequences.
 *
 * Architecture
 * ────────────
 *   Left input  ──► Left VN sequence  (seeded with seedL) ──► Left ER output
 *   Right input ──► Right VN sequence (seeded with seedR) ──► Right ER output
 *
 * Because the two sequences are seeded differently, the Left and Right ER
 * tap structures are completely decorrelated — there are no common tap
 * positions or common signs. This produces a natural, organic spatial
 * response: a hard-panned source triggers spatially distinct reflections
 * on each side, matching the physics of asymmetric room boundaries.
 *
 * Input signal routing
 * ────────────────────
 *   erOutL[n] = sum_k( sign_k_L * inputL[ n - tapDelay_k_L ] ) * normL
 *   erOutR[n] = sum_k( sign_k_R * inputR[ n - tapDelay_k_R ] ) * normR
 *
 * There is deliberately NO cross-feed between L and R here.  The FWHT
 * mixing inside the subsequent TV-FDN naturally spreads energy across
 * both channels during the diffuse tail.
 *
 * Real-time constraints
 * ─────────────────────
 *   • All memory (ring buffers, tap tables) is sized in prepare().
 *   • processBlock() is allocation-free and lock-free.
 *   • The inner loop walks a flat Tap array — branch-free, cache-friendly.
 */
class EarlyReflections
{
public:
    static constexpr int kMaxErLengthMs  = 120;    // absolute cap on window
    static constexpr int kMaxBlockSize   = 4096;

    EarlyReflections() = default;

    /**
     * Prepare both L and R velvet sequences and pre-allocate ring buffers.
     *
     * @param sampleRate       Host sample rate (Hz).
     * @param maxBlockSize     Maximum block size the host will call processBlock with.
     * @param erLengthMs       ER window length in milliseconds (0 < erLengthMs ≤ 120).
     * @param preDelayMs       Constant pre-delay before the first tap (0–50 ms).
     * @param targetDensityHz  Mean tap density for each sequence (impulses/sec).
     * @param minSpacingMs     Minimum inter-tap distance (prevents clustering).
     * @param seedL            PRNG seed for the Left channel sequence.
     * @param seedR            PRNG seed for the Right channel sequence.
     *                         Must differ from seedL for decorrelated output.
     */
    void prepare(double        sampleRate,
                 int           maxBlockSize,
                 float         erLengthMs,
                 float         preDelayMs,
                 float         targetDensityHz,
                 float         minSpacingMs,
                 std::uint32_t seedL,
                 std::uint32_t seedR);

    void reset() noexcept;

    /**
     * Process one block.  Writes ER output to erOutLeft / erOutRight.
     * Caller sums these with the dry signal before feeding the FDN.
     */
    void processBlock(const float* inLeft,
                      const float* inRight,
                      float*       erOutLeft,
                      float*       erOutRight,
                      int          numSamples) noexcept;

    // Parameter updates (audio-thread safe — atomic-style scalar writes)
    void setErMix(float gain) noexcept  { erMixGain_ = gain; }
    float erMix()             const noexcept { return erMixGain_; }

    const VelvetNoiseGenerator& generatorL() const noexcept { return genL_; }
    const VelvetNoiseGenerator& generatorR() const noexcept { return genR_; }

private:
    static int nextPowerOfTwo(int v) noexcept;

    // ── Ring buffer (one per channel) ────────────────────────────────────
    // Sized to kMaxErLengthSamples + maxBlockSize in prepare().
    std::vector<float> ringL_;
    std::vector<float> ringR_;
    int ringMask_      = 0;
    int writeIndexL_   = 0;
    int writeIndexR_   = 0;

    // ── Velvet sequences ─────────────────────────────────────────────────
    VelvetNoiseGenerator genL_;
    VelvetNoiseGenerator genR_;

    // ── Pre-delay offset (integer samples, applied to every tap) ─────────
    int preDelaySamples_ = 0;

    // ── Output gain (applied once after the tap sum) ──────────────────────
    // = erMixGain_ * normalisationGain, combined in prepare() and on set.
    float erMixGain_    = 0.3f; // user-facing wet level, 0..1
    float normGainL_    = 1.0f;
    float normGainR_    = 1.0f;

    bool prepared_ = false;
};
