#include "AbsorptionBank.h"

#include <algorithm>
#include <cmath>

// ── Internal constants ────────────────────────────────────────────────────────

static constexpr float kTwoPi = 6.283185307179586f;

// ── Helpers ───────────────────────────────────────────────────────────────────

float AbsorptionBank::t60ToLinearGain(float channelDelayTimeSec,
                                      float t60Sec) noexcept
{
    // Avoid division by zero / negative T60.
    if (t60Sec <= 0.0f || channelDelayTimeSec <= 0.0f)
        return kMaxLoopGain;

    // Formula: gain = 10^(-3 * D / T60)
    // At t = T60, this gain applied each loop yields -60 dB total attenuation.
    const float gain = std::pow(10.0f, -3.0f * (channelDelayTimeSec / t60Sec));

    return std::min(gain, kMaxLoopGain);
}

// ── RBJ Cookbook implementations ──────────────────────────────────────────────
//
// All formulas follow the Audio EQ Cookbook by Robert Bristow-Johnson.
// Coefficients are normalised by a0 before being passed to BiquadFilter.
//
// Sign convention: the TDF-II difference equation uses +a1, +a2 as stored here,
// matching the RBJ definition where the denominator is:
//   A(z) = 1 + a1·z^-1 + a2·z^-2   (a0 = 1 after normalisation)
//
// Note on A:
//   For shelves/peaks, A in the RBJ cookbook represents the LINEAR amplitude
//   gain at the shelf/peak: A = sqrt(10^(dBgain/20)) = sqrt(linearGain).
//   We receive `gain` as a linear power ratio < 1, so A = sqrt(gain).

void AbsorptionBank::computeLowShelf(double sampleRate, float freqHz, float gain) noexcept
{
    // Clamp gain defensively before sqrt.
    const float g = std::clamp(gain, 0.0f, kMaxLoopGain);

    const float A   = std::sqrt(g);                                // amplitude coefficient
    const float w0  = kTwoPi * freqHz / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float sqA  = std::sqrt(A);

    // alpha for S=1 (unity shelf slope):
    //   alpha = (sin(w0)/2) * sqrt( (A + 1/A)*(1/S - 1) + 2 )
    //         = (sin(w0)/2) * sqrt( (A + 1/A) + 2 )    when S=1, (1/1 - 1) = 0
    // Equivalent compact form: alpha = sin(w0) * sqrt(A) (often cited for S=1)
    // Using the full form from RBJ for correctness:
    const float alpha = (sinW / 2.0f) * std::sqrt((A + 1.0f / A) * (1.0f / 1.0f - 1.0f) + 2.0f);
    // When S=1 the inner sqrt simplifies to sqrt(2): alpha = sin(w0)/sqrt(2)
    // The formula above degenerates cleanly; keep it explicit for clarity.

    const float Ap1 = A + 1.0f;
    const float Am1 = A - 1.0f;

    const float b0 =    A * (Ap1 - Am1 * cosW + 2.0f * sqA * alpha);
    const float b1 =  2.0f * A * (Am1 - Ap1 * cosW);
    const float b2 =    A * (Ap1 - Am1 * cosW - 2.0f * sqA * alpha);
    const float a0 =         Ap1 + Am1 * cosW + 2.0f * sqA * alpha;
    const float a1 = -2.0f * (Am1 + Ap1 * cosW);
    const float a2 =         Ap1 + Am1 * cosW - 2.0f * sqA * alpha;

    const float inv_a0 = 1.0f / a0;
    lowShelf_.setCoefficients(b0 * inv_a0, b1 * inv_a0, b2 * inv_a0,
                              a1 * inv_a0, a2 * inv_a0);
}

void AbsorptionBank::computePeakEQ(double sampleRate, float freqHz,
                                   float gain, float Q) noexcept
{
    const float g = std::clamp(gain, 0.0f, kMaxLoopGain);

    const float A    = std::sqrt(g);
    const float w0   = kTwoPi * freqHz / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);

    // Clamp Q to a safe minimum to avoid division by near-zero.
    const float safeQ = std::max(Q, 0.01f);
    const float alpha  = sinW / (2.0f * safeQ);

    const float b0 =  1.0f + alpha * A;
    const float b1 = -2.0f * cosW;
    const float b2 =  1.0f - alpha * A;
    const float a0 =  1.0f + alpha / A;
    const float a1 = -2.0f * cosW;
    const float a2 =  1.0f - alpha / A;

    const float inv_a0 = 1.0f / a0;
    peak_.setCoefficients(b0 * inv_a0, b1 * inv_a0, b2 * inv_a0,
                          a1 * inv_a0, a2 * inv_a0);
}

void AbsorptionBank::computeHighShelf(double sampleRate, float freqHz, float gain) noexcept
{
    const float g = std::clamp(gain, 0.0f, kMaxLoopGain);

    const float A    = std::sqrt(g);
    const float w0   = kTwoPi * freqHz / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float sqA  = std::sqrt(A);

    const float alpha = (sinW / 2.0f) * std::sqrt((A + 1.0f / A) * (1.0f / 1.0f - 1.0f) + 2.0f);

    const float Ap1 = A + 1.0f;
    const float Am1 = A - 1.0f;

    const float b0 =    A * (Ap1 + Am1 * cosW + 2.0f * sqA * alpha);
    const float b1 = -2.0f * A * (Am1 + Ap1 * cosW);
    const float b2 =    A * (Ap1 + Am1 * cosW - 2.0f * sqA * alpha);
    const float a0 =         Ap1 - Am1 * cosW + 2.0f * sqA * alpha;
    const float a1 =  2.0f * (Am1 - Ap1 * cosW);
    const float a2 =         Ap1 - Am1 * cosW - 2.0f * sqA * alpha;

    const float inv_a0 = 1.0f / a0;
    highShelf_.setCoefficients(b0 * inv_a0, b1 * inv_a0, b2 * inv_a0,
                               a1 * inv_a0, a2 * inv_a0);
}

// ── Public interface ──────────────────────────────────────────────────────────

void AbsorptionBank::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    lowShelf_.prepare(sampleRate);
    peak_.prepare(sampleRate);
    highShelf_.prepare(sampleRate);
}

void AbsorptionBank::reset() noexcept
{
    lowShelf_.reset();
    peak_.reset();
    highShelf_.reset();
}

void AbsorptionBank::snapCoefficientsToTargets() noexcept
{
    lowShelf_.snapCoefficientsToTargets();
    peak_.snapCoefficientsToTargets();
    highShelf_.snapCoefficientsToTargets();
}

void AbsorptionBank::updateCoefficients(double sampleRate,
                                        float  channelDelayTimeSec,
                                        float  lowShelfFreqHz,  float t60LowSec,
                                        float  peakFreqHz,      float t60MidSec,  float peakQ,
                                        float  highShelfFreqHz, float t60HighSec) noexcept
{
    const float gainLow  = t60ToLinearGain(channelDelayTimeSec, t60LowSec);
    const float gainMid  = t60ToLinearGain(channelDelayTimeSec, t60MidSec);
    const float gainHigh = t60ToLinearGain(channelDelayTimeSec, t60HighSec);

    computeLowShelf (sampleRate, lowShelfFreqHz,  gainLow);
    computePeakEQ   (sampleRate, peakFreqHz,      gainMid,  peakQ);
    computeHighShelf(sampleRate, highShelfFreqHz, gainHigh);
}

float AbsorptionBank::processSample(float x) noexcept
{
    // Low Shelf → Peak/Bell → High Shelf (cascade, series connection).
    const float y = highShelf_.processSample(
                        peak_.processSample(
                            lowShelf_.processSample(x)));
    return y;
}
