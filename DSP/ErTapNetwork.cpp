#include "ErTapNetwork.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPi    = 3.14159265f;
constexpr float kTwoPi = 6.28318530f;

// Prime-ish ER delays (ms @ 44.1 kHz reference) — asymmetric L/R for width.
constexpr std::array<float, ErTapNetwork::kNumTaps> kTapDelayMsL {
    7.1f, 13.3f, 19.7f, 27.1f, 35.9f, 43.3f
};
constexpr std::array<float, ErTapNetwork::kNumTaps> kTapDelayMsR {
    11.3f, 17.9f, 23.7f, 31.1f, 39.7f, 44.9f
};

constexpr float kDecayTauMs   = 32.0f;  // exponential ER decay time constant
constexpr float kShelfCutoffHz = 5500.0f;
constexpr float kOutputNorm   = 0.22f;  // tap gain normalisation
}

int ErTapNetwork::nextPowerOfTwo(int v) noexcept
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

void ErTapNetwork::prepare(double sampleRate, int maxBlockSize)
{
    const float sr = static_cast<float>(sampleRate);

    int maxDelay = 0;
    for (int i = 0; i < kNumTaps; ++i)
    {
        const int dL = std::max(1, static_cast<int>(kTapDelayMsL[static_cast<std::size_t>(i)]
                                                    * sr * 0.001f));
        const int dR = std::max(1, static_cast<int>(kTapDelayMsR[static_cast<std::size_t>(i)]
                                                    * sr * 0.001f));
        maxDelay = std::max(maxDelay, std::max(dL, dR));

        const float decayL = std::exp(-kTapDelayMsL[static_cast<std::size_t>(i)] / kDecayTauMs);
        const float decayR = std::exp(-kTapDelayMsR[static_cast<std::size_t>(i)] / kDecayTauMs);
        tapsL_[static_cast<std::size_t>(i)] = { dL, decayL * kOutputNorm };
        tapsR_[static_cast<std::size_t>(i)] = { dR, decayR * kOutputNorm };
    }

    ringCapacity_ = nextPowerOfTwo(maxDelay + std::max(1, maxBlockSize) + 4);
    ringMask_     = ringCapacity_ - 1;
    ringL_.assign(static_cast<std::size_t>(ringCapacity_), 0.0f);
    ringR_.assign(static_cast<std::size_t>(ringCapacity_), 0.0f);

    shelfCoeff_ = 1.0f - std::exp(-kTwoPi * kShelfCutoffHz / sr);

    prepared_ = true;
    reset();
}

void ErTapNetwork::reset() noexcept
{
    std::fill(ringL_.begin(), ringL_.end(), 0.0f);
    std::fill(ringR_.begin(), ringR_.end(), 0.0f);
    writeIndexL_ = 0;
    writeIndexR_ = 0;
    shelfLpL_    = 0.0f;
    shelfLpR_    = 0.0f;
}

void ErTapNetwork::processBlock(const float* inLeft,
                                const float* inRight,
                                float*       outLeft,
                                float*       outRight,
                                int          numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    const int cap  = ringCapacity_;
    const int mask = ringMask_;

    for (int n = 0; n < numSamples; ++n)
    {
        ringL_[static_cast<std::size_t>(writeIndexL_)] = inLeft[n];
        ringR_[static_cast<std::size_t>(writeIndexR_)] = inRight[n];

        float sumL = 0.0f;
        float sumR = 0.0f;

        for (int t = 0; t < kNumTaps; ++t)
        {
            const auto& tapL = tapsL_[static_cast<std::size_t>(t)];
            const auto& tapR = tapsR_[static_cast<std::size_t>(t)];

            const int readL = (writeIndexL_ - tapL.delaySamples + cap) & mask;
            const int readR = (writeIndexR_ - tapR.delaySamples + cap) & mask;

            sumL += tapL.gain * ringL_[static_cast<std::size_t>(readL)];
            sumR += tapR.gain * ringR_[static_cast<std::size_t>(readR)];
        }

        // Gentle HF shelf: attenuate highs on the ER burst (wall absorption).
        shelfLpL_ += shelfCoeff_ * (sumL - shelfLpL_);
        shelfLpR_ += shelfCoeff_ * (sumR - shelfLpR_);
        const float hfL = sumL - shelfLpL_;
        const float hfR = sumR - shelfLpR_;
        outLeft[n]  = sumL - shelfMix_ * hfL;
        outRight[n] = sumR - shelfMix_ * hfR;

        writeIndexL_ = (writeIndexL_ + 1) & mask;
        writeIndexR_ = (writeIndexR_ + 1) & mask;
    }
}
