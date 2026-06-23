#include "BiquadFilter.h"

#include <cmath>

void BiquadFilter::prepare(double sampleRate) noexcept
{
    // One-pole smoothing coefficient for kSmoothingTimeMs time constant.
    // exp(-1 / (T_s * fs)) where T_s = smoothing time in seconds.
    const float tSamples = kSmoothingTimeMs * 0.001f * static_cast<float>(sampleRate);
    smCoeff_ = 1.0f - std::exp(-1.0f / tSamples);

    // Snap running values to targets so the first block has no initial glide.
    b0c_ = b0t_;
    b1c_ = b1t_;
    b2c_ = b2t_;
    a1c_ = a1t_;
    a2c_ = a2t_;

    reset();
}

void BiquadFilter::reset() noexcept
{
    s1_ = 0.0f;
    s2_ = 0.0f;
}

void BiquadFilter::setCoefficients(float b0, float b1, float b2,
                                   float a1, float a2) noexcept
{
    b0t_ = b0;
    b1t_ = b1;
    b2t_ = b2;
    a1t_ = a1;
    a2t_ = a2;
}

void BiquadFilter::snapCoefficientsToTargets() noexcept
{
    b0c_ = b0t_;
    b1c_ = b1t_;
    b2c_ = b2t_;
    a1c_ = a1t_;
    a2c_ = a2t_;
}

float BiquadFilter::processSample(float x) noexcept
{
    // ── Step 1: advance one-pole coefficient smoothers ───────────────────────
    // Each coefficient independently glides toward its target.
    // Five multiply-adds; no branches, no transcendental calls.
    b0c_ += smCoeff_ * (b0t_ - b0c_);
    b1c_ += smCoeff_ * (b1t_ - b1c_);
    b2c_ += smCoeff_ * (b2t_ - b2c_);
    a1c_ += smCoeff_ * (a1t_ - a1c_);
    a2c_ += smCoeff_ * (a2t_ - a2c_);

    // ── Step 2: TDF-II difference equations ─────────────────────────────────
    const float y = b0c_ * x + s1_;
    s1_ = b1c_ * x - a1c_ * y + s2_;
    s2_ = b2c_ * x - a2c_ * y;

    return y;
}
