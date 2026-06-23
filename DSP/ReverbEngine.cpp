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

    // ── Fractional pre-delay lines ────────────────────────────────────────────
    // Capacity covers the full 0–500 ms range at any sample rate.
    const int maxPreDelaySamples = static_cast<int>(
        kMaxPreDelayMs * static_cast<float>(sampleRate) * 0.001f) + 4;

    preDelayL_.prepare(maxPreDelaySamples);
    preDelayR_.prepare(maxPreDelaySamples);

    // One-pole smoothing coefficient for pre-delay time (~30 ms time constant).
    // Gives a natural tape-style Doppler glide during automation.
    preDelaySmCoeff_ = 1.0f - std::exp(
        -1.0f / (0.030f * static_cast<float>(sampleRate)));

    // Seed both target and current so prepare() produces no initial glide.
    const float clampedPreMs  = std::clamp(initialPreDelayMs, 0.0f, kMaxPreDelayMs);
    preDelayMsTarget_  = clampedPreMs;
    preDelayMsCurrent_ = clampedPreMs;

    // ── Distance + masterWet smoothing (~50 ms time constant each) ───────────
    const float coeff50ms = 1.0f - std::exp(
        -1.0f / (0.050f * static_cast<float>(sampleRate)));
    distanceSmoothCoeff_  = coeff50ms;
    masterWetSmCoeff_     = coeff50ms;

    // Snap smoothed values to their targets — no initial sweep on first block.
    distanceCurrent_   = distanceTarget_;
    masterWetCurrent_  = masterWetTarget_;

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
    er_.prepare(sampleRate, blockSize,
                erLengthMs,
                erDensityHz, erMinSpacingMs,
                seedL, seedR);

    fdn_.prepare(sampleRate, blockSize);
    fdn_.setDryWet(1.0f); // ReverbEngine owns the master dry/wet blend

    prepared_ = true;
    reset();
}

void ReverbEngine::reset() noexcept
{
    preDelayL_.reset();
    preDelayR_.reset();

    // Snap smoothed values to their targets — prevents a Doppler sweep or
    // distance sweep from silence when the DAW transport restarts.
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

void ReverbEngine::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    // Guard: scratch buffers are sized to maxBlockSize_. A DAW violating this
    // contract would write past the end — return silently rather than corrupt.
    if (numSamples > maxBlockSize_)
        return;

    const float srMs        = static_cast<float>(sampleRate_) * 0.001f;
    const float pdSmCoeff   = preDelaySmCoeff_;
    const float distSmCoeff = distanceSmoothCoeff_;
    const float wetSmCoeff  = masterWetSmCoeff_;

    // ── Step 1: Per-sample fractional pre-delay with Doppler glide ────────────
    //
    // preDelayMsCurrent_ tracks preDelayMsTarget_ via a one-pole filter.
    // Because FractionalDelayLine::readSample() takes a float, a slowly-moving
    // delay time produces the same pitch-shift artifact as a tape machine — the
    // exact behaviour desired when automating pre-delay in a DAW.
    //
    // The dry signal (left/right) is left untouched here. Only the delayed copy
    // written to delayedL_/R_ drives ER and FDN.
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayMsCurrent_ += pdSmCoeff * (preDelayMsTarget_ - preDelayMsCurrent_);

        // Convert ms to samples; FDL internally clamps to kMinStableDelaySamples
        const float delaySamples = preDelayMsCurrent_ * srMs;

        preDelayL_.writeSample(left[i]);
        preDelayR_.writeSample(right[i]);
        delayedL_[i] = preDelayL_.readSample(delaySamples);
        delayedR_[i] = preDelayR_.readSample(delaySamples);
    }

    // ── Step 2: Early reflections from the pre-delayed signal ─────────────────
    er_.processBlock(delayedL_.data(), delayedR_.data(),
                     erOutL_.data(), erOutR_.data(),
                     numSamples);

    // ── Step 3: Pre-delayed signal → FDN input (ER intentionally excluded) ──────
    //
    // We do NOT add erOutL/R here.  The Distance crossfade in Step 5 already
    // blends ER and the FDN tail at the output.  Feeding ER into the FDN would
    // seed the tank with early energy that then re-emerges in the diffuse tail,
    // causing ER content to appear twice and blurring the ER/tail boundary that
    // Distance is designed to control.  Keeping the paths orthogonal lets the
    // Distance knob crossfade cleanly between a close, room-shaped early field
    // and an enveloping, diffuse late tail.
    for (int i = 0; i < numSamples; ++i)
    {
        fdnInL_[i] = delayedL_[i];
        fdnInR_[i] = delayedR_[i];
    }

    // ── Step 4: FDN diffuse tail (100 % wet) ──────────────────────────────────
    fdn_.processBlock(fdnInL_.data(), fdnInR_.data(),
                      fdnOutL_.data(), fdnOutR_.data(),
                      numSamples);

    // ── Step 5: Per-sample smoothing + equal-power distance + wet blend ──────
    //
    // All three automatable parameters (distance, masterWet) are tracked by
    // one-pole lowpass filters and evaluated every sample, guaranteeing zero
    // zipper noise regardless of DAW automation resolution.
    //
    // Equal-power crossfade: erGain = cos(d * π/2), tailGain = sin(d * π/2).
    // At d=0: ER=1, Tail=0. At d=0.5: both=0.707 (-3 dB). At d=1: ER=0, Tail=1.
    // cos/sin are called once per sample — two transcendental calls total.
    constexpr float kHalfPi = 1.5707963267948966f;

    for (int i = 0; i < numSamples; ++i)
    {
        distanceCurrent_  += distSmCoeff  * (distanceTarget_  - distanceCurrent_);
        masterWetCurrent_ += wetSmCoeff * (masterWetTarget_ - masterWetCurrent_);

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
