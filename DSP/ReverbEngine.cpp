#include "ReverbEngine.h"

void ReverbEngine::prepare(double sampleRate, int maxBlockSize)
{
    dattorro_.prepare(sampleRate, std::max(1, maxBlockSize));
    prepared_ = true;
    reset();
}

void ReverbEngine::reset() noexcept
{
    dattorro_.reset();
}

void ReverbEngine::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    dattorro_.processBlock(left, right, numSamples);
}
