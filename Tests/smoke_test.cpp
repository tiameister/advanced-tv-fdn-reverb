#include "DSP/DattorroReverb.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    DattorroReverb engine;
    engine.prepare(sampleRate, blockSize);
    engine.setTime(2.5f);
    engine.setSize(0.5f);
    engine.setDamping(8000.0f);
    engine.setMix(1.0f);

    std::vector<float> left(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    float energy = 0.0f;
    for (int block = 0; block < 200; ++block)
    {
        engine.processBlock(left.data(), right.data(), blockSize);
        for (float s : left) energy += s * s;
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    std::printf("Output energy after impulse: %.6f\n", energy);

    if (!std::isfinite(energy) || energy <= 0.0f)
    {
        std::printf("FAIL: engine produced invalid output.\n");
        return 1;
    }

    std::printf("PASS: Dattorro smoke test completed.\n");
    return 0;
}
