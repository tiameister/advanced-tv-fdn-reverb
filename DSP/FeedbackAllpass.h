#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/**
 * Single-sample Schroeder allpass for use inside an FDN feedback path.
 * H(z) = (z^{-M} - g) / (1 - g·z^{-M})
 *
 * Prime delays in the 50–350 sample range (~1–8 ms at 44.1 kHz) smear the
 * critical 500 Hz – 2 kHz modal energy where metallic ringing is most audible,
 * without changing the overall spectral envelope (allpass property).
 */
class FeedbackAllpass
{
public:
    static constexpr int   kCapacity = 512; // must be power-of-2, fits max delay 331
    static constexpr float kGain     = 0.55f;

    void prepare(int delaySamples) noexcept
    {
        delaySamples_ = std::clamp(delaySamples, 2, kCapacity - 1);
        reset();
    }

    void reset() noexcept
    {
        buf_.fill(0.0f);
        writeIdx_ = 0;
    }

    float process(float x) noexcept
    {
        const int   readIdx = (writeIdx_ - delaySamples_ + kCapacity) & (kCapacity - 1);
        const float delayed = buf_[static_cast<std::size_t>(readIdx)];
        const float v       = x + kGain * delayed;
        buf_[static_cast<std::size_t>(writeIdx_)] = v;
        writeIdx_ = (writeIdx_ + 1) & (kCapacity - 1);
        return delayed - kGain * v;
    }

private:
    std::array<float, kCapacity> buf_{};
    int writeIdx_     = 0;
    int delaySamples_ = 3;
};
