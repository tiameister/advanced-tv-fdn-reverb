#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace dsp
{

constexpr bool isPowerOfTwo(std::size_t value) noexcept
{
    return value > 0 && (value & (value - 1)) == 0;
}

inline float orthogonalNormalization(std::size_t n) noexcept
{
    return 1.0f / std::sqrt(static_cast<float>(n));
}

/**
 * In-place Fast Walsh-Hadamard Transform for power-of-two channel counts.
 * Applies normalized orthogonal mixing: y = (1/sqrt(N)) * H_N * x
 */
template <std::size_t N>
inline void applyOrthogonalMix(std::array<float, N>& vec) noexcept
{
    static_assert(isPowerOfTwo(N), "FWHT requires a power-of-two channel count.");

    for (std::size_t stride = N / 2; stride > 0; stride /= 2)
    {
        for (std::size_t i = 0; i < N; i += stride * 2)
        {
            for (std::size_t j = 0; j < stride; ++j)
            {
                const float a = vec[i + j];
                const float b = vec[i + j + stride];
                vec[i + j] = a + b;
                vec[i + j + stride] = a - b;
            }
        }
    }

    const float norm = orthogonalNormalization(N);
    for (std::size_t i = 0; i < N; ++i)
        vec[i] *= norm;
}

} // namespace dsp
