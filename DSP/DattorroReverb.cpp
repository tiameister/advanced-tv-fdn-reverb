#include "DattorroReverb.h"

#include <cmath>

namespace
{
constexpr float kPi        = 3.14159265f;
constexpr float kTwoPi     = 6.28318530f;
constexpr float kLoopTime44100Sec =
    static_cast<float>(672 + 1800 + 908 + 2656) / 44100.0f;
}

// ── AllpassStage ───────────────────────────────────────────────────────────────

void DattorroReverb::AllpassStage::prepare(int delaySamples)
{
    M = std::max(1, delaySamples);
    int sz = 1;
    while (sz < M + 2) sz <<= 1;
    buf.assign(static_cast<std::size_t>(sz), 0.0f);
    cap = sz; mask = sz - 1; writeIdx = 0;
}

void DattorroReverb::AllpassStage::reset() noexcept
{
    std::fill(buf.begin(), buf.end(), 0.0f);
    writeIdx = 0;
}

float DattorroReverb::AllpassStage::process(float x, float g) noexcept
{
    const int   readIdx = (writeIdx - M + cap) & mask;
    const float dm      = buf[static_cast<std::size_t>(readIdx)];
    const float d       = x + g * dm;
    buf[static_cast<std::size_t>(writeIdx)] = d;
    writeIdx = (writeIdx + 1) & mask;
    return dm - g * d;
}

// ── ModulatedAllpassStage ────────────────────────────────────────────────────

void DattorroReverb::ModulatedAllpassStage::prepare(
    int baseDelaySamples, float modDepthSamples, float g) noexcept
{
    gain          = g;
    baseDelay     = std::max(1, baseDelaySamples);
    modDepthSamps = modDepthSamples;
    const int cap = baseDelay + static_cast<int>(std::ceil(modDepthSamples)) + 8;
    line.prepare(cap);
}

void DattorroReverb::ModulatedAllpassStage::reset() noexcept
{
    line.reset();
}

void DattorroReverb::ModulatedAllpassStage::setBaseDelay(int baseDelaySamples) noexcept
{
    baseDelay = std::max(1, baseDelaySamples);
}

float DattorroReverb::ModulatedAllpassStage::process(float x, float lfoSigned) noexcept
{
    const float delay = std::max(
        FractionalDelayLine::kMinStableDelaySamples,
        static_cast<float>(baseDelay) + modDepthSamps * lfoSigned);

    const float dm = line.readSample(delay);
    const float v  = x + gain * dm;
    line.writeSample(v);
    return dm - gain * v;
}

// ── IntegerDelay ─────────────────────────────────────────────────────────────

void DattorroReverb::IntegerDelay::prepare(int maxDelaySamples)
{
    int sz = 1;
    while (sz < maxDelaySamples + 2) sz <<= 1;
    buf.assign(static_cast<std::size_t>(sz), 0.0f);
    cap = sz; mask = sz - 1; writeIdx = 0;
}

void DattorroReverb::IntegerDelay::reset() noexcept
{
    std::fill(buf.begin(), buf.end(), 0.0f);
    writeIdx = 0;
}

float DattorroReverb::IntegerDelay::writeRead(float x, int delaySamples) noexcept
{
    buf[static_cast<std::size_t>(writeIdx)] = x;
    const int readIdx = (writeIdx - delaySamples + cap) & mask;
    writeIdx = (writeIdx + 1) & mask;
    return buf[static_cast<std::size_t>(readIdx)];
}

float DattorroReverb::IntegerDelay::peek(int delaySamples) const noexcept
{
    const int d = std::clamp(delaySamples, 1, cap - 1);
    const int readIdx = (writeIdx - d + cap) & mask;
    return buf[static_cast<std::size_t>(readIdx)];
}

// ── OnePoleLP / OnePoleHP ────────────────────────────────────────────────────

void DattorroReverb::OnePoleLP::setCutoff(float hz, double sampleRate) noexcept
{
    hz = std::clamp(hz, 20.0f, static_cast<float>(sampleRate * 0.49));
    coeff = 1.0f - std::exp(-kTwoPi * hz / static_cast<float>(sampleRate));
}

void DattorroReverb::OnePoleLP::reset() noexcept { z = 0.0f; }

float DattorroReverb::OnePoleLP::process(float x) noexcept
{
    z += coeff * (x - z);
    return z;
}

void DattorroReverb::OnePoleHP::setCutoff(float hz, double sampleRate) noexcept
{
    hz = std::clamp(hz, 20.0f, static_cast<float>(sampleRate * 0.49));
    coeff = 1.0f - std::exp(-kTwoPi * hz / static_cast<float>(sampleRate));
}

void DattorroReverb::OnePoleHP::reset() noexcept { lpState = 0.0f; }

float DattorroReverb::OnePoleHP::process(float x) noexcept
{
    lpState += coeff * (x - lpState);
    return x - lpState;
}

// ── DattorroReverb lifecycle ─────────────────────────────────────────────────

void DattorroReverb::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);

    const float srRatio = static_cast<float>(sampleRate / 44100.0);
    const float sr      = static_cast<float>(sampleRate);

    for (std::size_t i = 0; i < inputApL_.size(); ++i)
    {
        const int base = static_cast<int>(kInputApDelays[i] * srRatio) + 1;
        inputApL_[i].prepare(base);
        inputApR_[i].prepare(base + ((i & 1) == 0 ? 3 : 5));
    }

    const int maxTankDelay = static_cast<int>(
        static_cast<float>(kTankRD2) * srRatio * 2.0f) + 4;

    tankLD1_.prepare(maxTankDelay);
    tankLD2_.prepare(maxTankDelay);
    tankRD1_.prepare(maxTankDelay);
    tankRD2_.prepare(maxTankDelay);

    modDepthSamples_ = kApModDepthMs * sr * 0.001f;

    const int ap1BaseL = static_cast<int>(kTankLAp1 * srRatio) + 1;
    const int ap1BaseR = static_cast<int>(kTankRAp1 * srRatio) + 1;
    tankLAp1_.prepare(ap1BaseL, modDepthSamples_, kTankApGain);
    tankRAp1_.prepare(ap1BaseR, modDepthSamples_, kTankApGain);

    tankLAp2_.prepare(static_cast<int>(kTankLAp2 * srRatio) + 1);
    tankRAp2_.prepare(static_cast<int>(kTankRAp2 * srRatio) + 1);

    lfoLInc_ = kTwoPi * kLfoRateLHz / sr;
    lfoRInc_ = kTwoPi * kLfoRateRHz / sr;

    sizeSmCoeff_     = 1.0f - std::exp(-1.0f / (0.300f * sr));
    mixSmCoeff_      = 1.0f - std::exp(-1.0f / (0.030f * sr));
    dampingSmCoeff_  = 1.0f - std::exp(-1.0f / (0.050f * sr));

    hpfL_.setCutoff(kHpfCutoffHz, sampleRate_);
    hpfR_.setCutoff(kHpfCutoffHz, sampleRate_);

    sizeCurrent_       = sizeTarget_;
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
    for (auto& ap : inputApL_) ap.reset();
    for (auto& ap : inputApR_) ap.reset();
    tankLD1_.reset(); tankLD2_.reset();
    tankRD1_.reset(); tankRD2_.reset();
    tankLAp1_.reset(); tankRAp1_.reset();
    tankLAp2_.reset(); tankRAp2_.reset();
    lpfL_.reset(); lpfR_.reset();
    hpfL_.reset(); hpfR_.reset();
    lFeedback_ = 0.0f;
    rFeedback_ = 0.0f;
    lfoLPhase_ = 0.0f;
    lfoRPhase_ = 0.25f * kTwoPi;  // offset for decorrelation
    lfoLValue_ = 0.0f;
    lfoRValue_ = 0.0f;
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

void DattorroReverb::setPreDelayMs(float /*ms*/) noexcept
{
    // Pre-delay is owned by ReverbEngine.
}

void DattorroReverb::setMix(float wet01) noexcept
{
    mixTarget_ = std::clamp(wet01, 0.0f, 1.0f);
}

int DattorroReverb::scaleDelay(int baseSamples44100, float sizeFactor) const noexcept
{
    const float srRatio = static_cast<float>(sampleRate_ / 44100.0);
    return std::max(1, static_cast<int>(
        static_cast<float>(baseSamples44100) * srRatio * sizeFactor));
}

int DattorroReverb::tapSamples(int lineLen, float frac) const noexcept
{
    return std::max(1, static_cast<int>(static_cast<float>(lineLen) * frac));
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
    tankLD1Len_  = scaleDelay(kTankLD1, sizeFactor);
    tankLD2Len_  = scaleDelay(kTankLD2, sizeFactor);
    tankRD1Len_  = scaleDelay(kTankRD1, sizeFactor);
    tankRD2Len_  = scaleDelay(kTankRD2, sizeFactor);
    tankLAp1Len_ = scaleDelay(kTankLAp1, sizeFactor);
    tankRAp1Len_ = scaleDelay(kTankRAp1, sizeFactor);
    tankLAp1_.setBaseDelay(tankLAp1Len_);
    tankRAp1_.setBaseDelay(tankRAp1Len_);
    updateFeedbackGain();
}

void DattorroReverb::advanceLfos() noexcept
{
    lfoLPhase_ += lfoLInc_;
    lfoRPhase_ += lfoRInc_;
    if (lfoLPhase_ >= kTwoPi) lfoLPhase_ -= kTwoPi;
    if (lfoRPhase_ >= kTwoPi) lfoRPhase_ -= kTwoPi;
    lfoLValue_ = std::sin(lfoLPhase_);
    lfoRValue_ = std::sin(lfoRPhase_);
}

float DattorroReverb::computeWet(const OutputTap* taps, int numTaps,
                                 float lAp1, float rAp1) const noexcept
{
    float wet = 0.0f;
    for (int i = 0; i < numTaps; ++i)
    {
        const auto& t = taps[i];
        float sample  = 0.0f;

        switch (t.source)
        {
            case OutputTap::Source::delayLD1:
                sample = tankLD1_.peek(tapSamples(tankLD1Len_, t.delayFrac));
                break;
            case OutputTap::Source::delayLD2:
                sample = tankLD2_.peek(tapSamples(tankLD2Len_, t.delayFrac));
                break;
            case OutputTap::Source::delayRD1:
                sample = tankRD1_.peek(tapSamples(tankRD1Len_, t.delayFrac));
                break;
            case OutputTap::Source::delayRD2:
                sample = tankRD2_.peek(tapSamples(tankRD2Len_, t.delayFrac));
                break;
            case OutputTap::Source::signalLAp1:
                sample = lAp1;
                break;
            case OutputTap::Source::signalRAp1:
                sample = rAp1;
                break;
        }

        wet += t.sign * t.gain * sample;
    }
    return wet;
}

void DattorroReverb::processWetTail(const float* inLeft,
                                    const float* inRight,
                                    float*       wetLeft,
                                    float*       wetRight,
                                    int          numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0 || numSamples > maxBlockSize_)
        return;

    float prevSizeFactor = 0.5f + sizeCurrent_ * 1.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        advanceLfos();

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

        float inL = inLeft[i];
        float inR = inRight[i];
        for (std::size_t s = 0; s < inputApL_.size(); ++s)
        {
            inL = inputApL_[s].process(inL, kInputApGain);
            inR = inputApR_[s].process(inR, kInputApGain);
        }

        const float rFbPrev = rFeedback_;
        const float lFbPrev = lFeedback_;

        // ── Left tank ───────────────────────────────────────────────────────
        float l = inL + feedbackGain_ * rFbPrev;
        l = tankLD1_.writeRead(l, tankLD1Len_);
        const float lAp1 = tankLAp1_.process(l, lfoLValue_);
        l = lpfL_.process(hpfL_.process(lAp1));
        l = tankLD2_.writeRead(l, tankLD2Len_);
        l = tankLAp2_.process(l, kTankApGain);
        lFeedback_ = l;

        // ── Right tank ──────────────────────────────────────────────────────
        float r = inR + feedbackGain_ * lFbPrev;
        r = tankRD1_.writeRead(r, tankRD1Len_);
        const float rAp1 = tankRAp1_.process(r, lfoRValue_);
        r = lpfR_.process(hpfR_.process(rAp1));
        r = tankRD2_.writeRead(r, tankRD2Len_);
        r = tankRAp2_.process(r, kTankApGain);
        rFeedback_ = r;

        wetLeft[i]  = computeWet(kWetLTaps_.data(), kNumOutputTaps, lAp1, rAp1);
        wetRight[i] = computeWet(kWetRTaps_.data(), kNumOutputTaps, lAp1, rAp1);
    }
}

void DattorroReverb::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0 || numSamples > maxBlockSize_)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        dryL_[static_cast<std::size_t>(i)] = left[i];
        dryR_[static_cast<std::size_t>(i)] = right[i];
    }

    processWetTail(left, right, left, right, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        mixCurrent_ += mixSmCoeff_ * (mixTarget_ - mixCurrent_);

        const float dry = 1.0f - mixCurrent_;
        const float wet = mixCurrent_;

        left[i]  = dryL_[static_cast<std::size_t>(i)] * dry + left[i] * wet;
        right[i] = dryR_[static_cast<std::size_t>(i)] * dry + right[i] * wet;
    }
}
