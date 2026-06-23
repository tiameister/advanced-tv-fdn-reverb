#include "TVFDNEngine.h"

void TVFDNEngine::prepare(double sampleRate, int maxBlockSize)
{
    fdn_.prepare(sampleRate, std::max(1, maxBlockSize));
}

void TVFDNEngine::reset() noexcept
{
    fdn_.reset();
}

void TVFDNEngine::processBlock(float* left, float* right, int numSamples) noexcept
{
    processBlock(left, right, left, right, numSamples);
}

void TVFDNEngine::processBlock(const float* left,
                               const float* right,
                               float* outLeft,
                               float* outRight,
                               int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    fdn_.processBlock(left, right, outLeft, outRight, numSamples);
}

void TVFDNEngine::setFeedback(float feedback) noexcept
{
    fdn_.setFeedback(feedback);
}

void TVFDNEngine::setModDepth(float depthSamples) noexcept
{
    fdn_.setModDepth(depthSamples);
}

void TVFDNEngine::setDryWet(float mix) noexcept
{
    fdn_.setDryWet(mix);
}

void TVFDNEngine::setModRates(const std::array<float, 16>& ratesHz) noexcept
{
    fdn_.setModRates(ratesHz);
}
