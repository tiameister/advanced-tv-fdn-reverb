#pragma once

#include "DattorroReverb.h"
#include "ErTapNetwork.h"
#include "FractionalDelayLine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
 * Top-level reverb engine: pre-delay → input decorrelation → parallel ER + late tail.
 *
 * Wet blend (hardcoded for now): 40 % early reflections, 60 % Dattorro late tail.
 * A fraction of the ER output is fed into the late tank for acoustic glue.
 * All processing is allocation-free in processBlock().
 */
class ReverbEngine
{
public:
    ReverbEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void processBlock(float* left, float* right, int numSamples) noexcept;

    void setTime(float seconds) noexcept       { dattorro_.setTime(seconds); }
    void setSize(float size01) noexcept        { dattorro_.setSize(size01); }
    void setDamping(float hz) noexcept         { dattorro_.setDamping(hz); }
    void setPreDelayMs(float ms) noexcept;
    void setMix(float wet01) noexcept;

    const DattorroReverb& dattorro() const noexcept { return dattorro_; }

private:
    static constexpr float kMaxPreDelayMs  = 200.0f;
    static constexpr float kErWetShare     = 0.40f;
    static constexpr float kLateWetShare   = 0.60f;
    static constexpr float kErToLateFeed   = 0.25f;  // ER energy injected into late tank
    static constexpr float kDecorrDelayRms = 9.0f;

    struct AllpassStage
    {
        std::vector<float> buf;
        int cap = 0, mask = 0, writeIdx = 0, M = 0;

        void prepare(int delaySamples);
        void reset() noexcept;
        float process(float x, float g) noexcept;
    };

    void decorrelate(float inL, float inR, float& outL, float& outR) noexcept;

    DattorroReverb dattorro_;
    ErTapNetwork   er_;

    FractionalDelayLine preDelayL_;
    FractionalDelayLine preDelayR_;
    FractionalDelayLine decorrDelayR_;

    std::array<AllpassStage, 2> decorrApL_{};
    std::array<AllpassStage, 2> decorrApR_{};

    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 0;
    bool   prepared_     = false;

    float preDelayMsTarget_  = 0.0f;
    float preDelayMsCurrent_ = 0.0f;
    float mixTarget_         = 0.35f;
    float mixCurrent_        = 0.35f;
    float preDelaySmCoeff_   = 0.0f;
    float mixSmCoeff_        = 0.0f;
    float decorrDelaySamples_ = 0.0f;

    std::vector<float> dryL_;
    std::vector<float> dryR_;
    std::vector<float> decorL_;
    std::vector<float> decorR_;
    std::vector<float> erL_;
    std::vector<float> erR_;
    std::vector<float> lateL_;
    std::vector<float> lateR_;
};
