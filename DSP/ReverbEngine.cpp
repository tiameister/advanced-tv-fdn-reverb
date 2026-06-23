#include "ReverbEngine.h"

#include <algorithm>
#include <cmath>

void ReverbEngine::prepare(double        sampleRate,
                           int           maxBlockSize,
                           float         erLengthMs,
                           float         initialPreDelayMs,
                           float         erDensityHz,
                           float         erMinSpacingMs,
                           std::uint32_t seedL,
                           std::uint32_t seedR)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);
    const int blockSize = maxBlockSize_;

    // ── Store ER params for re-prepare ────────────────────────────────────────
    erLengthMs_     = std::clamp(erLengthMs,     20.0f,  200.0f);
    erDensityHz_    = std::clamp(erDensityHz,   500.0f, 8000.0f);
    erMinSpacingMs_ = erMinSpacingMs;
    erSeedL_        = seedL;
    erSeedR_        = seedR;

    // ── Fractional pre-delay lines ────────────────────────────────────────────
    const int maxPreDelaySamples = static_cast<int>(
        kMaxPreDelayMs * static_cast<float>(sampleRate) * 0.001f) + 4;

    preDelayL_.prepare(maxPreDelaySamples);
    preDelayR_.prepare(maxPreDelaySamples);

    // One-pole smoothing for pre-delay (~30 ms time constant → Doppler glide)
    preDelaySmCoeff_ = 1.0f - std::exp(
        -1.0f / (0.030f * static_cast<float>(sampleRate)));

    const float clampedPreMs  = std::clamp(initialPreDelayMs, 0.0f, kMaxPreDelayMs);
    preDelayMsTarget_  = clampedPreMs;
    preDelayMsCurrent_ = clampedPreMs;

    // ── Distance + masterWet smoothing (~50 ms time constant) ────────────────
    const float coeff50ms = 1.0f - std::exp(
        -1.0f / (0.050f * static_cast<float>(sampleRate)));
    distanceSmoothCoeff_ = coeff50ms;
    masterWetSmCoeff_    = coeff50ms;

    distanceCurrent_  = distanceTarget_;
    masterWetCurrent_ = masterWetTarget_;

    // ── Scratch buffers ───────────────────────────────────────────────────────
    const auto sz = static_cast<std::size_t>(blockSize);
    delayedL_.assign(sz, 0.0f);
    delayedR_.assign(sz, 0.0f);
    erOutL_  .assign(sz, 0.0f);
    erOutR_  .assign(sz, 0.0f);
    fdnInL_  .assign(sz, 0.0f);
    fdnInR_  .assign(sz, 0.0f);
    fdnOutL_ .assign(sz, 0.0f);
    fdnOutR_ .assign(sz, 0.0f);

    // ── Sub-modules ───────────────────────────────────────────────────────────
    er_.prepare(sampleRate_, blockSize,
                erLengthMs_, erDensityHz_, erMinSpacingMs_,
                erSeedL_, erSeedR_);

    fdn_.prepare(sampleRate_, blockSize);
    fdn_.setDryWet(1.0f); // ReverbEngine owns the master dry/wet blend

    erNeedsUpdate_.store(false, std::memory_order_relaxed);
    prepared_ = true;
    reset();
}

void ReverbEngine::reset() noexcept
{
    preDelayL_.reset();
    preDelayR_.reset();

    preDelayMsCurrent_ = preDelayMsTarget_;
    distanceCurrent_   = distanceTarget_;
    masterWetCurrent_  = masterWetTarget_;

    er_.reset();
    fdn_.reset();

    std::fill(delayedL_.begin(), delayedL_.end(), 0.0f);
    std::fill(delayedR_.begin(), delayedR_.end(), 0.0f);
    std::fill(erOutL_.begin(),   erOutL_.end(),   0.0f);
    std::fill(erOutR_.begin(),   erOutR_.end(),   0.0f);
    std::fill(fdnInL_.begin(),   fdnInL_.end(),   0.0f);
    std::fill(fdnInR_.begin(),   fdnInR_.end(),   0.0f);
    std::fill(fdnOutL_.begin(),  fdnOutL_.end(),  0.0f);
    std::fill(fdnOutR_.begin(),  fdnOutR_.end(),  0.0f);
}

// ── ER parameter setters ───────────────────────────────────────────────────────

void ReverbEngine::setErLength(float ms) noexcept
{
    const float clamped = std::clamp(ms, 20.0f, 200.0f);
    if (std::abs(clamped - erLengthMs_) > 0.5f)
    {
        erLengthMs_ = clamped;
        erNeedsUpdate_.store(true, std::memory_order_release);
    }
}

void ReverbEngine::setErDensity(float hz) noexcept
{
    const float clamped = std::clamp(hz, 500.0f, 8000.0f);
    if (std::abs(clamped - erDensityHz_) > 1.0f)
    {
        erDensityHz_ = clamped;
        erNeedsUpdate_.store(true, std::memory_order_release);
    }
}

// ── processBlock ──────────────────────────────────────────────────────────────

void ReverbEngine::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    if (numSamples > maxBlockSize_)
        return;

    // ── ER re-prepare (one-shot on parameter change) ─────────────────────────
    // erNeedsUpdate_ is set from the UI thread by setErLength / setErDensity.
    // er_.prepare() allocates (ring buffer resize) — acceptable here because
    // ER length/density are quasi-static room-character parameters, not
    // automatable. The brief glitch is far less disruptive than a full reset.
    if (erNeedsUpdate_.exchange(false, std::memory_order_acq_rel))
    {
        er_.prepare(sampleRate_, maxBlockSize_,
                    erLengthMs_, erDensityHz_, erMinSpacingMs_,
                    erSeedL_, erSeedR_);
    }

    const float srMs        = static_cast<float>(sampleRate_) * 0.001f;
    const float pdSmCoeff   = preDelaySmCoeff_;
    const float distSmCoeff = distanceSmoothCoeff_;
    const float wetSmCoeff  = masterWetSmCoeff_;

    // ── Step 1: Per-sample fractional pre-delay ───────────────────────────────
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayMsCurrent_ += pdSmCoeff * (preDelayMsTarget_ - preDelayMsCurrent_);
        const float delaySamples = preDelayMsCurrent_ * srMs;

        preDelayL_.writeSample(left[i]);
        preDelayR_.writeSample(right[i]);
        delayedL_[i] = preDelayL_.readSample(delaySamples);
        delayedR_[i] = preDelayR_.readSample(delaySamples);
    }

    // ── Step 2: Early reflections ─────────────────────────────────────────────
    er_.processBlock(delayedL_.data(), delayedR_.data(),
                     erOutL_.data(), erOutR_.data(),
                     numSamples);

    // ── Step 3: Pre-delayed signal → FDN input (ER intentionally excluded) ────
    //
    // ER is kept OUT of the FDN input.  The Distance crossfade at Step 5 already
    // blends ER and tail at the output.  Coupling them here would seed the tank
    // with early energy that re-emerges in the diffuse tail, making early content
    // appear twice and blurring the ER/tail boundary Distance controls.
    for (int i = 0; i < numSamples; ++i)
    {
        fdnInL_[i] = delayedL_[i];
        fdnInR_[i] = delayedR_[i];
    }

    // ── Step 4: FDN diffuse tail (100 % wet) ──────────────────────────────────
    fdn_.processBlock(fdnInL_.data(), fdnInR_.data(),
                      fdnOutL_.data(), fdnOutR_.data(),
                      numSamples);

    // ── Step 5: Per-sample equal-power distance crossfade + master wet blend ──
    //
    // Equal-power law: erGain = cos(d·π/2), tailGain = sin(d·π/2).
    // At d=0.5: both = 0.707 (−3 dB) — no perceived loudness dip.
    constexpr float kHalfPi = 1.5707963267948966f;

    for (int i = 0; i < numSamples; ++i)
    {
        distanceCurrent_  += distSmCoeff * (distanceTarget_  - distanceCurrent_);
        masterWetCurrent_ += wetSmCoeff  * (masterWetTarget_ - masterWetCurrent_);

        const float erGain   = std::cos(distanceCurrent_ * kHalfPi);
        const float tailGain = std::sin(distanceCurrent_ * kHalfPi);
        const float wet      = masterWetCurrent_;
        const float dry      = 1.0f - wet;

        const float wetL = erOutL_[i] * erGain + fdnOutL_[i] * tailGain;
        const float wetR = erOutR_[i] * erGain + fdnOutR_[i] * tailGain;
        left[i]  = left[i]  * dry + wetL * wet;
        right[i] = right[i] * dry + wetR * wet;
    }
}
