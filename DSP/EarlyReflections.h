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
 * Pre-delay is NOT handled here. The caller (ReverbEngine) must apply a
 * global pre-delay ring to the input BEFORE calling processBlock so that
 * the FDN tail and the ER burst both start from the same delayed moment.
 *
 * Bitwise wrapping
 * ────────────────
 * Read indices are computed as:
 *   (writeIndex - tapDelay + ringCapacity_) & ringMask_
 * The +ringCapacity_ term guarantees the left operand of & is always positive,
 * avoiding undefined behaviour from bitwise AND on negative signed integers.
 */
class EarlyReflections
{
public:
    static constexpr int kMaxErLengthMs = 120;  // absolute cap on ER window

    EarlyReflections() = default;

    /**
     * Prepare both L and R velvet sequences and pre-allocate ring buffers.
     *
     * @param sampleRate       Host sample rate (Hz).
     * @param maxBlockSize     Maximum block size the host will call processBlock with.
     * @param erLengthMs       ER window length in milliseconds (0 < erLengthMs ≤ 120).
     * @param targetDensityHz  Mean tap density for each sequence (impulses/sec).
     * @param minSpacingMs     Minimum inter-tap distance (prevents clustering).
     * @param seedL            PRNG seed for the Left channel sequence.
     * @param seedR            PRNG seed for the Right channel sequence.
     *                         Must differ from seedL for decorrelated output.
     */
    void prepare(double        sampleRate,
                 int           maxBlockSize,
                 float         erLengthMs,
                 float         targetDensityHz,
                 float         minSpacingMs,
                 std::uint32_t seedL,
                 std::uint32_t seedR);

    void reset() noexcept;

    /**
     * Process one block.  Writes ER output to erOutLeft / erOutRight.
     * Input must already be pre-delayed by the caller if pre-delay is desired.
     */
    void processBlock(const float* inLeft,
                      const float* inRight,
                      float*       erOutLeft,
                      float*       erOutRight,
                      int          numSamples) noexcept;

    /** Raw ER output level before Distance crossfade (0..1, default 1). */
    void  setErMix(float gain) noexcept { erMixGain_ = gain; }
    float erMix()              const noexcept { return erMixGain_; }

    const VelvetNoiseGenerator& generatorL() const noexcept { return genL_; }
    const VelvetNoiseGenerator& generatorR() const noexcept { return genR_; }

private:
    static int nextPowerOfTwo(int v) noexcept;

    std::vector<float> ringL_;
    std::vector<float> ringR_;
    int ringCapacity_  = 0;   // stored explicitly for safe negative-offset reads
    int ringMask_      = 0;
    int writeIndexL_   = 0;
    int writeIndexR_   = 0;

    VelvetNoiseGenerator genL_;
    VelvetNoiseGenerator genR_;

    float erMixGain_ = 1.0f;  // distance crossfade in ReverbEngine handles blend
    float normGainL_ = 1.0f;
    float normGainR_ = 1.0f;

    bool prepared_ = false;
};
