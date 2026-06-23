#include "ReverbEngine.h"

#include <algorithm>
#include <cmath>

int ReverbEngine::nextPowerOfTwo(int v) noexcept
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

void ReverbEngine::setDistance(float d) noexcept
{
    distance_       = std::clamp(d, 0.0f, 1.0f);
    constexpr float kHalfPi = 1.5707963267948966f;
    // Computed here (setup/UI thread) — never in the audio thread
    erGainCached_   = std::cos(distance_ * kHalfPi);
    tailGainCached_ = std::sin(distance_ * kHalfPi);
}

void ReverbEngine::prepare(double        sampleRate,
                           int           maxBlockSize,
                           float         erLengthMs,
                           float         preDelayMs,
                           float         erDensityHz,
                           float         erMinSpacingMs,
                           std::uint32_t seedL,
                           std::uint32_t seedR)
{
    const int blockSize = std::max(1, maxBlockSize);

    // ── Pre-delay ring ────────────────────────────────────────────────────────
    const float clampedPreMs = std::clamp(preDelayMs, 0.0f, 500.0f);
    preDelaySamples_ = static_cast<int>(
        clampedPreMs * static_cast<float>(sampleRate) * 0.001f);
    const int preDelayCapacity = nextPowerOfTwo(preDelaySamples_ + blockSize + 2);
    preDelayMask_ = preDelayCapacity - 1;

    preDelayRingL_.assign(static_cast<std::size_t>(preDelayCapacity), 0.0f);
    preDelayRingR_.assign(static_cast<std::size_t>(preDelayCapacity), 0.0f);
    preDelayWriteL_ = 0;
    preDelayWriteR_ = 0;

    // ── Scratch buffers ───────────────────────────────────────────────────────
    const auto sz = static_cast<std::size_t>(blockSize);
    delayedL_.assign(sz, 0.0f);
    delayedR_.assign(sz, 0.0f);
    erOutL_.assign(sz, 0.0f);
    erOutR_.assign(sz, 0.0f);
    fdnInL_.assign(sz, 0.0f);
    fdnInR_.assign(sz, 0.0f);
    fdnOutL_.assign(sz, 0.0f);
    fdnOutR_.assign(sz, 0.0f);

    // ── Sub-modules ───────────────────────────────────────────────────────────
    // EarlyReflections no longer takes preDelayMs — timing is handled here.
    er_.prepare(sampleRate, blockSize,
                erLengthMs,
                erDensityHz, erMinSpacingMs,
                seedL, seedR);

    fdn_.prepare(sampleRate, blockSize);
    fdn_.setDryWet(1.0f); // ReverbEngine owns the master dry/wet

    // Cache initial distance gains
    setDistance(distance_);

    prepared_ = true;
    reset();
}

void ReverbEngine::reset() noexcept
{
    er_.reset();
    fdn_.reset();

    std::fill(preDelayRingL_.begin(), preDelayRingL_.end(), 0.0f);
    std::fill(preDelayRingR_.begin(), preDelayRingR_.end(), 0.0f);
    preDelayWriteL_ = 0;
    preDelayWriteR_ = 0;

    std::fill(delayedL_.begin(), delayedL_.end(), 0.0f);
    std::fill(delayedR_.begin(), delayedR_.end(), 0.0f);
    std::fill(erOutL_.begin(),   erOutL_.end(),   0.0f);
    std::fill(erOutR_.begin(),   erOutR_.end(),   0.0f);
    std::fill(fdnInL_.begin(),   fdnInL_.end(),   0.0f);
    std::fill(fdnInR_.begin(),   fdnInR_.end(),   0.0f);
    std::fill(fdnOutL_.begin(),  fdnOutL_.end(),  0.0f);
    std::fill(fdnOutR_.begin(),  fdnOutR_.end(),  0.0f);
}

void ReverbEngine::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    const int   pdMask    = preDelayMask_;
    const int   pdSamples = preDelaySamples_;

    // ── Step 1: Global pre-delay ──────────────────────────────────────────────
    // Write dry into the ring and read the delayed version into scratch.
    // The master dry (left/right) remains untouched for the output blend.
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayRingL_[static_cast<std::size_t>(preDelayWriteL_)] = left[i];
        preDelayRingR_[static_cast<std::size_t>(preDelayWriteR_)] = right[i];

        const int rdL = (preDelayWriteL_ - pdSamples + pdMask + 1) & pdMask;
        const int rdR = (preDelayWriteR_ - pdSamples + pdMask + 1) & pdMask;
        delayedL_[i] = preDelayRingL_[static_cast<std::size_t>(rdL)];
        delayedR_[i] = preDelayRingR_[static_cast<std::size_t>(rdR)];

        preDelayWriteL_ = (preDelayWriteL_ + 1) & pdMask;
        preDelayWriteR_ = (preDelayWriteR_ + 1) & pdMask;
    }

    // ── Step 2: Early reflections from the pre-delayed signal ─────────────────
    er_.processBlock(delayedL_.data(), delayedR_.data(),
                     erOutL_.data(), erOutR_.data(),
                     numSamples);

    // ── Step 3: Feed pre-delayed signal into FDN (seeded with ER) ─────────────
    for (int i = 0; i < numSamples; ++i)
    {
        fdnInL_[i] = delayedL_[i] + erOutL_[i];
        fdnInR_[i] = delayedR_[i] + erOutR_[i];
    }

    // ── Step 4: FDN tail (100 % wet, into dedicated output buffers) ───────────
    fdn_.processBlock(fdnInL_.data(), fdnInR_.data(),
                      fdnOutL_.data(), fdnOutR_.data(),
                      numSamples);

    // ── Step 5: Distance crossfade + master dry/wet blend ─────────────────────
    // erGainCached_ and tailGainCached_ are pre-computed in setDistance().
    // No sin/cos in the audio thread.
    const float erGain   = erGainCached_;
    const float tailGain = tailGainCached_;
    const float wet      = masterWet_;
    const float dry      = 1.0f - wet;

    for (int i = 0; i < numSamples; ++i)
    {
        const float wetL = erOutL_[i] * erGain + fdnOutL_[i] * tailGain;
        const float wetR = erOutR_[i] * erGain + fdnOutR_[i] * tailGain;
        left[i]  = left[i]  * dry + wetL * wet;
        right[i] = right[i] * dry + wetR * wet;
    }
}
