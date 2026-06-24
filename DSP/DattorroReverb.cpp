#include "DattorroReverb.h"

#include <cmath>

namespace
{
constexpr float kLoopTime44100Sec =
    static_cast<float>(672 + 1800 + 908 + 2656) / 44100.0f;
}

void DattorroReverb::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);

    const float srRatio = static_cast<float>(sampleRate / 44100.0);

    for (std::size_t i = 0; i < inputAp_.size(); ++i)
        inputAp_[i].prepare(static_cast<int>(kInputApDelays[i] * srRatio) + 1);

    const int maxTankDelay = static_cast<int>(
        static_cast<float>(kTankRD2) * srRatio * 2.0f) + 4;

    tankLD1_.prepare(maxTankDelay);
    tankLD2_.prepare(maxTankDelay);
    tankRD1_.prepare(maxTankDelay);
    tankRD2_.prepare(maxTankDelay);

    tankLAp1_.prepare(static_cast<int>(kTankLAp1 * srRatio) + 1);
    tankLAp2_.prepare(static_cast<int>(kTankLAp2 * srRatio) + 1);
    tankRAp1_.prepare(static_cast<int>(kTankRAp1 * srRatio) + 1);
    tankRAp2_.prepare(static_cast<int>(kTankRAp2 * srRatio) + 1);

    const int maxPreDelaySamples = static_cast<int>(
        kMaxPreDelayMs * static_cast<float>(sampleRate) * 0.001f) + 4;
    preDelayL_.prepare(maxPreDelaySamples);
    preDelayR_.prepare(maxPreDelaySamples);

    const float sr = static_cast<float>(sampleRate);
    sizeSmCoeff_     = 1.0f - std::exp(-1.0f / (0.300f * sr));
    preDelaySmCoeff_ = 1.0f - std::exp(-1.0f / (0.030f * sr));
    mixSmCoeff_      = 1.0f - std::exp(-1.0f / (0.030f * sr));
    dampingSmCoeff_  = 1.0f - std::exp(-1.0f / (0.050f * sr));

    sizeCurrent_       = sizeTarget_;
    preDelayMsCurrent_ = preDelayMsTarget_;
    mixCurrent_        = mixTarget_;
    dampingHzCurrent_  = dampingHzTarget_;

    dryL_.assign(static_cast<std::size_t>(maxBlockSize_), 0.0f);
    dryR_.assign(static_cast<std::size_t>(maxBlockSize_), 0.0f);

    rebuildDelayLengths(0.5f + sizeCurrent_ * 1.5f);
    updateFeedbackGain();
    updateDampingCoeff();

    prepared_ = true;
    reset();
}

void DattorroReverb::reset() noexcept
{
    for (auto& ap : inputAp_) ap.reset();
    tankLD1_.reset(); tankLD2_.reset();
    tankRD1_.reset(); tankRD2_.reset();
    tankLAp1_.reset(); tankLAp2_.reset();
    tankRAp1_.reset(); tankRAp2_.reset();
    lpfL_.reset(); lpfR_.reset();
    preDelayL_.reset(); preDelayR_.reset();
    lFeedback_ = 0.0f;
    rFeedback_ = 0.0f;
    preDelayMsCurrent_ = preDelayMsTarget_;
    mixCurrent_        = mixTarget_;
    sizeCurrent_       = sizeTarget_;
    dampingHzCurrent_  = dampingHzTarget_;
}

void DattorroReverb::setTime(float seconds) noexcept
{
    timeSecTarget_ = std::clamp(seconds, kMinTimeSec, kMaxTimeSec);
    updateFeedbackGain();
}

void DattorroReverb::setSize(float size01) noexcept
{
    sizeTarget_ = std::clamp(size01, 0.0f, 1.0f);
}

void DattorroReverb::setDamping(float hz) noexcept
{
    dampingHzTarget_ = std::clamp(hz, kMinDampingHz, kMaxDampingHz);
}

void DattorroReverb::setPreDelayMs(float ms) noexcept
{
    preDelayMsTarget_ = std::clamp(ms, 0.0f, kMaxPreDelayMs);
}

void DattorroReverb::setMix(float wet01) noexcept
{
    mixTarget_ = std::clamp(wet01, 0.0f, 1.0f);
}

int DattorroReverb::scaleDelay(int baseSamples44100, float sizeFactor) const noexcept
{
    const float srRatio = static_cast<float>(sampleRate_ / 44100.0);
    return std::max(1, static_cast<int>(static_cast<float>(baseSamples44100)
                                        * srRatio * sizeFactor));
}

void DattorroReverb::updateFeedbackGain() noexcept
{
    const float sizeFactor = 0.5f + sizeCurrent_ * 1.5f;
    const float loopTime   = kLoopTime44100Sec * sizeFactor;
    const float t60        = std::max(timeSecTarget_, kMinTimeSec);
    feedbackGain_ = std::pow(10.0f, -3.0f * loopTime / t60);
    feedbackGain_ = std::clamp(feedbackGain_, 0.0f, 0.98f);
}

void DattorroReverb::updateDampingCoeff() noexcept
{
    lpfL_.setCutoff(dampingHzCurrent_, sampleRate_);
    lpfR_.setCutoff(dampingHzCurrent_, sampleRate_);
}

void DattorroReverb::rebuildDelayLengths(float sizeFactor) noexcept
{
    tankLD1Len_ = scaleDelay(kTankLD1, sizeFactor);
    tankLD2Len_ = scaleDelay(kTankLD2, sizeFactor);
    tankRD1Len_ = scaleDelay(kTankRD1, sizeFactor);
    tankRD2Len_ = scaleDelay(kTankRD2, sizeFactor);
    updateFeedbackGain();
}

void DattorroReverb::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0 || numSamples > maxBlockSize_)
        return;

    const float srMs = static_cast<float>(sampleRate_) * 0.001f;

    for (int i = 0; i < numSamples; ++i)
    {
        dryL_[static_cast<std::size_t>(i)] = left[i];
        dryR_[static_cast<std::size_t>(i)] = right[i];
    }

    float prevSizeFactor = 0.5f + sizeCurrent_ * 1.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        sizeCurrent_ += sizeSmCoeff_ * (sizeTarget_ - sizeCurrent_);
        const float sizeFactor = 0.5f + sizeCurrent_ * 1.5f;
        if (std::abs(sizeFactor - prevSizeFactor) > 0.002f)
        {
            rebuildDelayLengths(sizeFactor);
            prevSizeFactor = sizeFactor;
        }

        dampingHzCurrent_ += dampingSmCoeff_ * (dampingHzTarget_ - dampingHzCurrent_);
        if (i == 0)
            updateDampingCoeff();

        preDelayMsCurrent_ += preDelaySmCoeff_ * (preDelayMsTarget_ - preDelayMsCurrent_);
        mixCurrent_        += mixSmCoeff_ * (mixTarget_ - mixCurrent_);

        const float delaySamples = preDelayMsCurrent_ * srMs;
        preDelayL_.writeSample(dryL_[static_cast<std::size_t>(i)]);
        preDelayR_.writeSample(dryR_[static_cast<std::size_t>(i)]);
        const float inL = preDelayL_.readSample(delaySamples);
        const float inR = preDelayR_.readSample(delaySamples);

        float mono = 0.5f * (inL + inR);
        for (auto& ap : inputAp_)
            mono = ap.process(mono, kInputApGain);

        const float rFbPrev = rFeedback_;
        const float lFbPrev = lFeedback_;

        float l = mono + feedbackGain_ * rFbPrev;
        l = tankLD1_.writeRead(l, tankLD1Len_);
        l = tankLAp1_.process(l, kTankApGain);
        l = lpfL_.process(l);
        const float lTap = l;
        l = tankLD2_.writeRead(l, tankLD2Len_);
        l = tankLAp2_.process(l, kTankApGain);

        float r = mono + feedbackGain_ * lFbPrev;
        r = tankRD1_.writeRead(r, tankRD1Len_);
        r = tankRAp1_.process(r, kTankApGain);
        r = lpfR_.process(r);
        const float rTap = r;
        r = tankRD2_.writeRead(r, tankRD2Len_);
        r = tankRAp2_.process(r, kTankApGain);

        lFeedback_ = l;
        rFeedback_ = r;

        const float wetL = lTap + rTap * 0.35f;
        const float wetR = rTap + lTap * 0.35f;
        const float dry  = 1.0f - mixCurrent_;
        const float wet  = mixCurrent_;

        left[i]  = dryL_[static_cast<std::size_t>(i)] * dry + wetL * wet;
        right[i] = dryR_[static_cast<std::size_t>(i)] * dry + wetR * wet;
    }
}
