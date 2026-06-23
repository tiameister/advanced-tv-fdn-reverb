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

void TVFDNEngine::setReverbTime(float reverbTimeSec) noexcept
{
    fdn_.setReverbTime(reverbTimeSec);
}

void TVFDNEngine::setDecayShape(float bassDecayMult,
                                float midDecayMult,
                                float hfDecayMult) noexcept
{
    fdn_.setDecayShape(bassDecayMult, midDecayMult, hfDecayMult);
}

void TVFDNEngine::setSize(float size) noexcept
{
    fdn_.setSize(size);
}

void TVFDNEngine::setModDepth(float depthSamples) noexcept
{
    fdn_.setModDepth(depthSamples);
}

void TVFDNEngine::setDryWet(float mix) noexcept
{
    fdn_.setDryWet(mix);
}

void TVFDNEngine::setStereoWidth(float width) noexcept
{
    fdn_.setStereoWidth(width);
}

void TVFDNEngine::setModRates(const std::array<float, 16>& ratesHz) noexcept
{
    fdn_.setModRates(ratesHz);
}
