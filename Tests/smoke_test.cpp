#include "DSP/TVFDNEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    TVFDNEngine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setFeedback(0.85f);
    engine.setModDepth(4.0f);
    engine.setDryWet(1.0f);

    const auto& delays = engine.getFDN().getBaseDelaySamples();
    std::printf("Prime delay lengths @ %.0f Hz:\n", sampleRate);
    for (std::size_t i = 0; i < delays.size(); ++i)
    {
        const float ms = static_cast<float>(delays[i]) / static_cast<float>(sampleRate) * 1000.0f;
        std::printf("  ch%02zu: %d samples (%.3f ms)\n", i, delays[i], ms);
    }

    std::vector<float> left(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    std::vector<float> outL(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(blockSize), 0.0f);

    for (int block = 0; block < 200; ++block)
        engine.processBlock(left.data(), right.data(), outL.data(), outR.data(), blockSize);

    float energy = 0.0f;
    for (float sample : outL)
        energy += sample * sample;

    std::printf("Output energy after impulse: %.6f\n", energy);

    if (!std::isfinite(energy) || energy <= 0.0f)
    {
        std::printf("FAIL: engine produced invalid output.\n");
        return 1;
    }

    std::printf("PASS: TV-FDN smoke test completed.\n");
    return 0;
}
