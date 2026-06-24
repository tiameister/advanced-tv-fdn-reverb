#include "ReverbEngine.h"

namespace
{
constexpr float kTwoPi = 6.28318530f;

constexpr std::array<float, 2> kDecorrApDelaysMsL { 3.7f, 5.9f };
constexpr std::array<float, 2> kDecorrApDelaysMsR { 6.1f, 4.3f };
constexpr std::array<float, 2> kDecorrApGainL     { 0.38f, 0.42f };
constexpr std::array<float, 2> kDecorrApGainR     { 0.44f, 0.36f };
}

void ReverbEngine::AllpassStage::prepare(int delaySamples)
{
    M = std::max(1, delaySamples);
    int sz = 1;
    while (sz < M + 2) sz <<= 1;
    buf.assign(static_cast<std::size_t>(sz), 0.0f);
    cap = sz; mask = sz - 1; writeIdx = 0;
}

void ReverbEngine::AllpassStage::reset() noexcept
{
    std::fill(buf.begin(), buf.end(), 0.0f);
    writeIdx = 0;
}

float ReverbEngine::AllpassStage::process(float x, float g) noexcept
{
    const int   readIdx = (writeIdx - M + cap) & mask;
    const float dm      = buf[static_cast<std::size_t>(readIdx)];
    const float d       = x + g * dm;
    buf[static_cast<std::size_t>(writeIdx)] = d;
    writeIdx = (writeIdx + 1) & mask;
    return dm - g * d;
}

void ReverbEngine::setPreDelayMs(float ms) noexcept
{
    preDelayMsTarget_ = std::clamp(ms, 0.0f, kMaxPreDelayMs);
}

void ReverbEngine::setMix(float wet01) noexcept
{
    mixTarget_ = std::clamp(wet01, 0.0f, 1.0f);
}

void ReverbEngine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);

    const float sr = static_cast<float>(sampleRate);

    dattorro_.prepare(sampleRate, maxBlockSize_);
    er_.prepare(sampleRate, maxBlockSize_);

    const int maxPreDelaySamples =
        static_cast<int>(kMaxPreDelayMs * sr * 0.001f) + 4;
    preDelayL_.prepare(maxPreDelaySamples);
    preDelayR_.prepare(maxPreDelaySamples);

    const int decorrDelaySamples =
        std::max(1, static_cast<int>(kDecorrDelayRms * sr * 0.001f));
    decorrDelaySamples_ = static_cast<float>(decorrDelaySamples);
    decorrDelayR_.prepare(decorrDelaySamples + 4);

    for (std::size_t i = 0; i < decorrApL_.size(); ++i)
    {
        const int dL = std::max(1, static_cast<int>(kDecorrApDelaysMsL[i] * sr * 0.001f));
        const int dR = std::max(1, static_cast<int>(kDecorrApDelaysMsR[i] * sr * 0.001f));
        decorrApL_[i].prepare(dL);
        decorrApR_[i].prepare(dR);
    }

    preDelaySmCoeff_ = 1.0f - std::exp(-1.0f / (0.030f * sr));
    mixSmCoeff_      = 1.0f - std::exp(-1.0f / (0.030f * sr));

    preDelayMsCurrent_ = preDelayMsTarget_;
    mixCurrent_        = mixTarget_;

    const std::size_t n = static_cast<std::size_t>(maxBlockSize_);
    dryL_.assign(n, 0.0f);
    dryR_.assign(n, 0.0f);
    decorL_.assign(n, 0.0f);
    decorR_.assign(n, 0.0f);
    erL_.assign(n, 0.0f);
    erR_.assign(n, 0.0f);
    lateL_.assign(n, 0.0f);
    lateR_.assign(n, 0.0f);

    prepared_ = true;
    reset();
}

void ReverbEngine::reset() noexcept
{
    dattorro_.reset();
    er_.reset();
    preDelayL_.reset();
    preDelayR_.reset();
    decorrDelayR_.reset();
    for (auto& ap : decorrApL_) ap.reset();
    for (auto& ap : decorrApR_) ap.reset();
    preDelayMsCurrent_ = preDelayMsTarget_;
    mixCurrent_        = mixTarget_;
}

void ReverbEngine::decorrelate(float inL, float inR, float& outL, float& outR) noexcept
{
    decorrDelayR_.writeSample(inR);
    const float delayedR = decorrDelayR_.readSample(decorrDelaySamples_);

    float l = inL;
    float r = delayedR;
    l = decorrApL_[0].process(l, kDecorrApGainL[0]);
    l = decorrApL_[1].process(l, kDecorrApGainL[1]);
    r = decorrApR_[0].process(r, kDecorrApGainR[0]);
    r = decorrApR_[1].process(r, kDecorrApGainR[1]);
    outL = l;
    outR = r;
}

void ReverbEngine::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0 || numSamples > maxBlockSize_)
        return;

    const float srMs = static_cast<float>(sampleRate_) * 0.001f;

    for (int i = 0; i < numSamples; ++i)
    {
        dryL_[static_cast<std::size_t>(i)] = left[i];
        dryR_[static_cast<std::size_t>(i)] = right[i];
    }

    for (int i = 0; i < numSamples; ++i)
    {
        preDelayMsCurrent_ += preDelaySmCoeff_ * (preDelayMsTarget_ - preDelayMsCurrent_);
        mixCurrent_        += mixSmCoeff_ * (mixTarget_ - mixCurrent_);

        const float delaySamples = preDelayMsCurrent_ * srMs;

        preDelayL_.writeSample(dryL_[static_cast<std::size_t>(i)]);
        preDelayR_.writeSample(dryR_[static_cast<std::size_t>(i)]);
        const float pdL = preDelayL_.readSample(delaySamples);
        const float pdR = preDelayR_.readSample(delaySamples);

        float dL = 0.0f, dR = 0.0f;
        decorrelate(pdL, pdR, dL, dR);
        decorL_[static_cast<std::size_t>(i)] = dL;
        decorR_[static_cast<std::size_t>(i)] = dR;
    }

    er_.processBlock(decorL_.data(), decorR_.data(),
                     erL_.data(), erR_.data(), numSamples);

    dattorro_.processWetTail(decorL_.data(), decorR_.data(),
                             lateL_.data(), lateR_.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float wetL = kErWetShare * erL_[static_cast<std::size_t>(i)]
                         + kLateWetShare * lateL_[static_cast<std::size_t>(i)];
        const float wetR = kErWetShare * erR_[static_cast<std::size_t>(i)]
                         + kLateWetShare * lateR_[static_cast<std::size_t>(i)];

        const float dry = 1.0f - mixCurrent_;
        const float wet = mixCurrent_;

        left[i]  = dryL_[static_cast<std::size_t>(i)] * dry + wetL * wet;
        right[i] = dryR_[static_cast<std::size_t>(i)] * dry + wetR * wet;
    }
}
