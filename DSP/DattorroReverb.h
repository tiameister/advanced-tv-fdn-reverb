#pragma once

#include "FractionalDelayLine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Dattorro figure-eight plate reverb with modern density enhancements.
 *
 * Input:  stereo 4-stage Schroeder diffusers → per-channel tank injection
 * Tanks:  cross-fed L/R loops, modulated AP1, static AP2, HPF+LPF damping
 * Output: 6-tap signed extraction network per channel (decorrelated stereo)
 *
 * Pre-delay, input decorrelation, and ER are handled by ReverbEngine.
 */
class DattorroReverb
{
public:
    DattorroReverb() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Late tail only — writes wet L/R (no dry/mix). */
    void processWetTail(const float* inLeft,
                        const float* inRight,
                        float*       wetLeft,
                        float*       wetRight,
                        int          numSamples) noexcept;

    /** Convenience path: wet tail + dry/wet mix (no pre-delay). */
    void processBlock(float* left, float* right, int numSamples) noexcept;

    void setTime(float seconds) noexcept;
    void setSize(float size01) noexcept;
    void setDamping(float hz) noexcept;
    void setPreDelayMs(float ms) noexcept;  // no-op; pre-delay lives in ReverbEngine
    void setMix(float wet01) noexcept;

private:
    static constexpr float kMaxPreDelayMs   = 200.0f;  // retained for API compat
    static constexpr float kMinTimeSec      = 0.1f;
    static constexpr float kMaxTimeSec      = 20.0f;
    static constexpr float kMinDampingHz    = 500.0f;
    static constexpr float kMaxDampingHz    = 20000.0f;
    static constexpr float kHpfCutoffHz     = 100.0f;
    static constexpr float kLfoRateLHz      = 0.9f;
    static constexpr float kLfoRateRHz      = 1.1f;
    static constexpr float kApModDepthMs    = 1.25f;  // max LFO excursion on AP1 delays
    static constexpr int   kNumOutputTaps  = 6;

    // Canonical delay lengths (samples @ 44.1 kHz) — Dattorro Effect Design Part 1
    static constexpr std::array<int, 4> kInputApDelays { 142, 107, 379, 277 };
    static constexpr float              kInputApGain   = 0.75f;
    static constexpr float              kTankApGain  = 0.5f;
    static constexpr int                kTankLD1     = 672;
    static constexpr int                kTankLAp1    = 445;
    static constexpr int                kTankLD2     = 1800;
    static constexpr int                kTankLAp2    = 378;
    static constexpr int                kTankRD1     = 908;
    static constexpr int                kTankRAp1    = 324;
    static constexpr int                kTankRD2     = 2656;
    static constexpr int                kTankRAp2    = 110;

    struct AllpassStage
    {
        std::vector<float> buf;
        int cap = 0, mask = 0, writeIdx = 0, M = 0;

        void prepare(int delaySamples);
        void reset() noexcept;
        float process(float x, float g) noexcept;
    };

    /** Schroeder allpass with Hermite fractional read (for LFO-modulated AP1). */
    struct ModulatedAllpassStage
    {
        FractionalDelayLine line;
        float gain          = 0.5f;
        int   baseDelay     = 1;
        float modDepthSamps = 0.0f;

        void prepare(int baseDelaySamples, float modDepthSamples, float g) noexcept;
        void reset() noexcept;
        void setBaseDelay(int baseDelaySamples) noexcept;
        float process(float x, float lfoSigned) noexcept;
    };

    struct IntegerDelay
    {
        std::vector<float> buf;
        int cap = 0, mask = 0, writeIdx = 0;

        void prepare(int maxDelaySamples);
        void reset() noexcept;
        float writeRead(float x, int delaySamples) noexcept;
        float peek(int delaySamples) const noexcept;
    };

    struct OnePoleLP
    {
        float z     = 0.0f;
        float coeff = 0.0f;

        void setCutoff(float hz, double sampleRate) noexcept;
        void reset() noexcept;
        float process(float x) noexcept;
    };

    /** y = x − LP(x)  one-pole high-pass (DC / mud cleanup in feedback path). */
    struct OnePoleHP
    {
        float lpState = 0.0f;
        float coeff   = 0.0f;

        void setCutoff(float hz, double sampleRate) noexcept;
        void reset() noexcept;
        float process(float x) noexcept;
    };

    struct OutputTap
    {
        enum class Source : std::uint8_t
        {
            delayLD1, delayLD2, delayRD1, delayRD2,
            signalLAp1, signalRAp1
        };

        Source source;
        float  delayFrac;  // fraction of line length (ignored for signal taps)
        float  gain;
        float  sign;       // +1 or −1
    };

    int  scaleDelay(int baseSamples44100, float sizeFactor) const noexcept;
    int  tapSamples(int lineLen, float frac) const noexcept;
    void updateFeedbackGain() noexcept;
    void updateDampingCoeff() noexcept;
    void rebuildDelayLengths(float sizeFactor) noexcept;
    void advanceLfos() noexcept;
    float computeWet(const OutputTap* taps, int numTaps,
                     float lAp1, float rAp1) const noexcept;

    std::array<AllpassStage, 4> inputApL_{};
    std::array<AllpassStage, 4> inputApR_{};
    IntegerDelay tankLD1_{}, tankLD2_{}, tankRD1_{}, tankRD2_{};
    ModulatedAllpassStage tankLAp1_{}, tankRAp1_{};
    AllpassStage tankLAp2_{}, tankRAp2_{};
    OnePoleLP  lpfL_{}, lpfR_{};
    OnePoleHP  hpfL_{}, hpfR_{};

    float lFeedback_ = 0.0f;
    float rFeedback_ = 0.0f;

    int tankLD1Len_  = kTankLD1;
    int tankLD2Len_  = kTankLD2;
    int tankRD1Len_  = kTankRD1;
    int tankRD2Len_  = kTankRD2;
    int tankLAp1Len_ = kTankLAp1;
    int tankRAp1Len_ = kTankRAp1;

    float lfoLPhase_ = 0.0f;
    float lfoRPhase_ = 0.0f;
    float lfoLInc_   = 0.0f;
    float lfoRInc_   = 0.0f;
    float lfoLValue_ = 0.0f;
    float lfoRValue_ = 0.0f;
    float modDepthSamples_ = 0.0f;

    static constexpr std::array<OutputTap, kNumOutputTaps> kWetLTaps_{{
        { OutputTap::Source::delayLD1,  0.37f, 0.26f,  1.0f },
        { OutputTap::Source::signalLAp1, 0.0f, 0.32f,  1.0f },
        { OutputTap::Source::delayLD2,  0.59f, 0.21f, -1.0f },
        { OutputTap::Source::delayRD1,  0.43f, 0.18f,  1.0f },
        { OutputTap::Source::signalRAp1, 0.0f, 0.24f, -1.0f },
        { OutputTap::Source::delayRD2,  0.67f, 0.15f,  1.0f },
    }};

    static constexpr std::array<OutputTap, kNumOutputTaps> kWetRTaps_{{
        { OutputTap::Source::delayRD1,  0.31f, 0.26f,  1.0f },
        { OutputTap::Source::signalRAp1, 0.0f, 0.32f,  1.0f },
        { OutputTap::Source::delayRD2,  0.53f, 0.21f, -1.0f },
        { OutputTap::Source::delayLD1,  0.47f, 0.18f,  1.0f },
        { OutputTap::Source::signalLAp1, 0.0f, 0.24f, -1.0f },
        { OutputTap::Source::delayLD2,  0.71f, 0.15f,  1.0f },
    }};

    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 0;
    bool   prepared_     = false;

    float timeSecTarget_     = 2.5f;
    float sizeTarget_        = 0.33f;
    float dampingHzTarget_   = 8000.0f;
    float mixTarget_         = 0.35f;

    float sizeCurrent_       = 0.33f;
    float mixCurrent_        = 0.35f;
    float feedbackGain_      = 0.65f;
    float dampingHzCurrent_  = 8000.0f;

    float sizeSmCoeff_     = 0.0f;
    float mixSmCoeff_      = 0.0f;
    float dampingSmCoeff_  = 0.0f;

    std::vector<float> dryL_;
    std::vector<float> dryR_;
};
