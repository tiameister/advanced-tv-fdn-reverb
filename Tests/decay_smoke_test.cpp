#include "DSP/ReverbEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;
    constexpr int    numBlocks  = static_cast<int>(2.5 * sampleRate / blockSize);

    ReverbEngine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setTime(2.5f);
    engine.setSize(0.6f);
    engine.setDamping(2000.0f);
    engine.setMix(1.0f);

    std::vector<float> left(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.0f);
    left[0] = right[0] = 1.0f;

    float peakEarly = 0.0f;
    float energyLate = 0.0f;
    const int lateStart = numBlocks / 2;

    for (int block = 0; block < numBlocks; ++block)
    {
        engine.processBlock(left.data(), right.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float e = std::abs(left[static_cast<std::size_t>(i)]);
            if (block < 4) peakEarly = std::max(peakEarly, e);
            if (block >= lateStart)
                energyLate += left[static_cast<std::size_t>(i)] * left[static_cast<std::size_t>(i)];
        }
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    std::printf("Peak early: %.5f  Late energy: %.5f\n", peakEarly, energyLate);

    if (!std::isfinite(peakEarly) || peakEarly <= 0.0f)
    {
        std::printf("FAIL: no early response.\n");
        return 1;
    }

    std::printf("PASS: decay smoke test completed.\n");
    return 0;
}
