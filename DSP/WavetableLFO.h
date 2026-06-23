#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace dsp
{
namespace detail
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

constexpr float wrapToTwoPi(float radians) noexcept
{
    float wrapped = radians;
    while (wrapped >= kTwoPi)
        wrapped -= kTwoPi;
    while (wrapped < 0.0f)
        wrapped += kTwoPi;
    return wrapped;
}

constexpr float wrapToPi(float radians) noexcept
{
    float wrapped = radians;
    while (wrapped > kPi)
        wrapped -= kTwoPi;
    while (wrapped < -kPi)
        wrapped += kTwoPi;
    return wrapped;
}

constexpr float constexprSin(float radians) noexcept
{
    const float x = wrapToPi(radians);
    const float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f))));
}

template <int TableSize>
constexpr std::array<float, static_cast<std::size_t>(TableSize) + 1u> makeSineTable() noexcept
{
    std::array<float, static_cast<std::size_t>(TableSize) + 1u> table{};
    for (int i = 0; i <= TableSize; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(TableSize);
        table[static_cast<std::size_t>(i)] = constexprSin(kTwoPi * phase);
    }
    return table;
}

} // namespace detail
} // namespace dsp

/**
 * Sine wavetable LFO with fixed-point phase accumulation.
 * Uses a compile-time immutable wavetable — no global mutable state.
 * No sin()/cos() calls on the audio thread.
 */
class WavetableLFO
{
public:
    static constexpr int kTableSize = 2048;
    static constexpr std::uint32_t kPhaseScale = 0xFFFFFFFFu;

    WavetableLFO() = default;

    void prepare(double sampleRate, float frequencyHz, float initialPhaseRadians);
    void reset() noexcept;

    void setFrequency(float frequencyHz) noexcept;
    void setPhase(float phaseRadians) noexcept;

    float getCurrentValue() const noexcept;
    float advance(int numSamples) noexcept;

private:
    inline static constexpr auto kSineTable = dsp::detail::makeSineTable<kTableSize>();

    double sampleRate_ = 44100.0;
    std::uint32_t phase_ = 0;
    std::uint32_t phaseIncrement_ = 0;

    float lookup(std::uint32_t phase) const noexcept;
};

inline void WavetableLFO::prepare(double sampleRate, float frequencyHz, float initialPhaseRadians)
{
    sampleRate_ = sampleRate;
    setFrequency(frequencyHz);
    setPhase(initialPhaseRadians);
}

inline void WavetableLFO::reset() noexcept
{
    phase_ = 0;
}

inline void WavetableLFO::setFrequency(float frequencyHz) noexcept
{
    const double normalized = static_cast<double>(frequencyHz) / sampleRate_;
    phaseIncrement_ = static_cast<std::uint32_t>(normalized * static_cast<double>(kPhaseScale));
}

inline void WavetableLFO::setPhase(float phaseRadians) noexcept
{
    const float wrapped = dsp::detail::wrapToTwoPi(phaseRadians);
    const float normalized = wrapped / dsp::detail::kTwoPi;
    phase_ = static_cast<std::uint32_t>(static_cast<double>(normalized) * static_cast<double>(kPhaseScale));
}

inline float WavetableLFO::getCurrentValue() const noexcept
{
    return lookup(phase_);
}

inline float WavetableLFO::advance(int numSamples) noexcept
{
    phase_ += phaseIncrement_ * static_cast<std::uint32_t>(numSamples);
    return lookup(phase_);
}

inline float WavetableLFO::lookup(std::uint32_t phase) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(phase >> 21);
    const std::uint32_t frac = (phase >> 9) & 0xFFFu;
    const float sample0 = kSineTable[index];
    const float sample1 = kSineTable[index + 1u];
    const float fraction = static_cast<float>(frac) / 4096.0f;
    return sample0 + fraction * (sample1 - sample0);
}
