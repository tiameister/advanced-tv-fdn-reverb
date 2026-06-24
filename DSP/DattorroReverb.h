#pragma once

#include "FractionalDelayLine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Classic Dattorro figure-eight plate reverb (JUCE-free, RT-safe).
 *
 * Input: 4-stage Schroeder diffuser → mono tank injection
 * Tanks:  cross-fed L/R loops with AP + one-pole LP damping
 * Output: stereo taps from both tanks
 */
class DattorroReverb
{
public:
    DattorroReverb() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void processBlock(float* left, float* right, int numSamples) noexcept;

    void setTime(float seconds) noexcept;
    void setSize(float size01) noexcept;
    void setDamping(float hz) noexcept;
    void setPreDelayMs(float ms) noexcept;
    void setMix(float wet01) noexcept;

private:
    static constexpr float kMaxPreDelayMs = 200.0f;
    static constexpr float kMinTimeSec    = 0.1f;
    static constexpr float kMaxTimeSec    = 20.0f;
    static constexpr float kMinDampingHz  = 500.0f;
    static constexpr float kMaxDampingHz  = 20000.0f;

    // Canonical delay lengths (samples @ 44.1 kHz) from Dattorro Effect Design Part 1
    static constexpr std::array<int, 4> kInputApDelays   { 142, 107, 379, 277 };
    static constexpr float              kInputApGain     = 0.75f;
    static constexpr float              kTankApGain      = 0.5f;
    static constexpr int                kTankLD1         = 672;
    static constexpr int                kTankLAp1        = 445;
    static constexpr int                kTankLD2         = 1800;
    static constexpr int                kTankLAp2        = 378;
    static constexpr int                kTankRD1         = 908;
    static constexpr int                kTankRAp1        = 324;
    static constexpr int                kTankRD2         = 2656;
    static constexpr int                kTankRAp2        = 110;

    struct AllpassStage
    {
        std::vector<float> buf;
        int cap = 0, mask = 0, writeIdx = 0, M = 0;

        void prepare(int delaySamples)
        {
            M = std::max(1, delaySamples);
            int sz = 1;
            while (sz < M + 2) sz <<= 1;
            buf.assign(static_cast<std::size_t>(sz), 0.0f);
            cap = sz; mask = sz - 1; writeIdx = 0;
        }

        void reset() noexcept
        {
            std::fill(buf.begin(), buf.end(), 0.0f);
            writeIdx = 0;
        }

        float process(float x, float g) noexcept
        {
            const int   readIdx = (writeIdx - M + cap) & mask;
            const float dm      = buf[static_cast<std::size_t>(readIdx)];
            const float d       = x + g * dm;
            buf[static_cast<std::size_t>(writeIdx)] = d;
            writeIdx = (writeIdx + 1) & mask;
            return dm - g * d;
        }
    };

    struct IntegerDelay
    {
        std::vector<float> buf;
        int cap = 0, mask = 0, writeIdx = 0;

        void prepare(int maxDelaySamples)
        {
            int sz = 1;
            while (sz < maxDelaySamples + 2) sz <<= 1;
            buf.assign(static_cast<std::size_t>(sz), 0.0f);
            cap = sz; mask = sz - 1; writeIdx = 0;
        }

        void reset() noexcept
        {
            std::fill(buf.begin(), buf.end(), 0.0f);
            writeIdx = 0;
        }

        float writeRead(float x, int delaySamples) noexcept
        {
            buf[static_cast<std::size_t>(writeIdx)] = x;
            const int readIdx = (writeIdx - delaySamples + cap) & mask;
            writeIdx = (writeIdx + 1) & mask;
            return buf[static_cast<std::size_t>(readIdx)];
        }
    };

    struct OnePoleLP
    {
        float z = 0.0f;
        float coeff = 0.0f;

        void setCutoff(float hz, double sampleRate) noexcept
        {
            hz = std::clamp(hz, 20.0f, static_cast<float>(sampleRate * 0.49));
            coeff = 1.0f - std::exp(-2.0f * 3.14159265f * hz / static_cast<float>(sampleRate));
        }

        void reset() noexcept { z = 0.0f; }

        float process(float x) noexcept
        {
            z += coeff * (x - z);
            return z;
        }
    };

    int scaleDelay(int baseSamples44100, float sizeFactor) const noexcept;
    void updateFeedbackGain() noexcept;
    void updateDampingCoeff() noexcept;
    void rebuildDelayLengths(float sizeFactor) noexcept;

    std::array<AllpassStage, 4> inputAp_{};
    IntegerDelay tankLD1_{}, tankLD2_{}, tankRD1_{}, tankRD2_{};
    AllpassStage tankLAp1_{}, tankLAp2_{}, tankRAp1_{}, tankRAp2_{};
    OnePoleLP    lpfL_{}, lpfR_{};

    FractionalDelayLine preDelayL_;
    FractionalDelayLine preDelayR_;

    float lFeedback_ = 0.0f;
    float rFeedback_ = 0.0f;

    int tankLD1Len_ = kTankLD1;
    int tankLD2Len_ = kTankLD2;
    int tankRD1Len_ = kTankRD1;
    int tankRD2Len_ = kTankRD2;

    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 0;
    bool   prepared_     = false;

    float timeSecTarget_  = 2.5f;
    float sizeTarget_     = 0.33f;
    float dampingHzTarget_ = 8000.0f;
    float preDelayMsTarget_  = 0.0f;
    float mixTarget_         = 0.35f;

    float sizeCurrent_       = 0.33f;
    float preDelayMsCurrent_ = 0.0f;
    float mixCurrent_        = 0.35f;
    float feedbackGain_      = 0.65f;
    float dampingHzCurrent_  = 8000.0f;

    float sizeSmCoeff_       = 0.0f;
    float preDelaySmCoeff_   = 0.0f;
    float mixSmCoeff_        = 0.0f;
    float dampingSmCoeff_    = 0.0f;

    std::vector<float> dryL_;
    std::vector<float> dryR_;
};
