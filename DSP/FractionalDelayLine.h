#pragma once

#include <cstddef>
#include <vector>

/**
 * Lock-free circular delay line with 3rd-order Hermite (Catmull-Rom) fractional
 * read interpolation.
 *
 * Interpolation choice
 * ────────────────────
 * Hermite cubic is chosen over Thiran allpass for modulated contexts (LFO
 * delay-time sweeps, pre-delay automation). Thiran is an IIR filter whose
 * state variable (`apState_`) accumulates error when the fractional coefficient
 * changes from sample to sample, producing audible pitch warble and HF grit.
 * Hermite is a stateless FIR computation on four consecutive buffer taps —
 * the fractional coefficient can change freely every sample with zero artefacts.
 *
 * Minimum delay
 * ─────────────
 * Hermite requires reading samples at offsets [-1, 0, +1, +2] relative to the
 * integer delay tap. The "+2" offset means we must stay at least 2 full integer
 * samples behind the write pointer to avoid reading unwritten data.
 * kMinStableDelaySamples = 2.0f enforces this.
 *
 * Index safety
 * ────────────
 * bufferCapacity_ (= buffer_.size(), a power of two ≥ maxDelaySamples + 2) is
 * added to every read offset before masking. Because bufferCapacity_ is always
 * larger than the maximum delay plus the Hermite look-ahead, the pre-mask value
 * is guaranteed non-negative — no sign extension, no UB from casting a negative
 * signed int to std::size_t.
 *
 * Memory
 * ──────
 * All allocation happens in prepare(). writeSample/readSample are allocation-free.
 */
class FractionalDelayLine
{
public:
    static constexpr float kMinStableDelaySamples = 2.0f;

    FractionalDelayLine() = default;

    void prepare(int maxDelaySamples);
    void reset() noexcept;

    void  writeSample(float sample) noexcept;
    float readSample(float delayInSamples) noexcept;

private:
    static int nextPowerOfTwo(int value) noexcept;

    static float hermite4(float t, float xm1, float x0, float x1, float x2) noexcept;

    std::vector<float> buffer_;
    int bufferCapacity_ = 0;  // power of two; stored to guarantee positive index
    int bufferMask_     = 0;  // bufferCapacity_ - 1
    int writeIndex_     = 0;
};
