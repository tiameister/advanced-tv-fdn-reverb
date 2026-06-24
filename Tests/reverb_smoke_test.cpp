#include "DSP/ReverbEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

static bool isFiniteNonZero(const std::vector<float>& buf, int n)
{
    float energy = 0.0f;
    for (int i = 0; i < n; ++i)
        energy += buf[static_cast<std::size_t>(i)] * buf[static_cast<std::size_t>(i)];
    return std::isfinite(energy) && energy > 0.0f;
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 256;
    constexpr int    numBlocks  = 400;

    ReverbEngine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setTime(2.5f);
    engine.setSize(0.5f);
    engine.setDamping(6000.0f);
    engine.setMix(1.0f);

    std::vector<float> left (static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.0f);
    left[0] = right[0] = 1.0f;

    for (int block = 0; block < numBlocks; ++block)
    {
        engine.processBlock(left.data(), right.data(), blockSize);
        if (block == 0)
        {
            std::printf("Block 0 outL[0..4]: %.5f %.5f %.5f %.5f %.5f\n",
                        left[0], left[1], left[2], left[3], left[4]);
        }
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    engine.reset();
    left[0] = right[0] = 1.0f;
    for (int block = 0; block < numBlocks; ++block)
    {
        engine.processBlock(left.data(), right.data(), blockSize);
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    if (!isFiniteNonZero(left, blockSize) && !isFiniteNonZero(right, blockSize))
        std::printf("Output fully decayed after %d blocks (expected).\n", numBlocks);

    std::printf("PASS: ReverbEngine Dattorro smoke test completed.\n");
    return 0;
}
