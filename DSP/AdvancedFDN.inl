#include "AdvancedFDN.h"

#include <algorithm>

template <int NumChannels>
int AdvancedFDN<NumChannels>::nearestAvailablePrime(int target,
                                                    const std::vector<bool>& isPrimeTable,
                                                    std::array<bool, 4096>& used) noexcept
{
    const int maxIndex = static_cast<int>(isPrimeTable.size()) - 1;

    for (int offset = 0; offset <= maxIndex; ++offset)
    {
        const int upper = target + offset;
        if (upper <= maxIndex && isPrimeTable[static_cast<std::size_t>(upper)] && !used[static_cast<std::size_t>(upper)])
        {
            used[static_cast<std::size_t>(upper)] = true;
            return upper;
        }

        const int lower = target - offset;
        if (lower >= 2 && isPrimeTable[static_cast<std::size_t>(lower)] && !used[static_cast<std::size_t>(lower)])
        {
            used[static_cast<std::size_t>(lower)] = true;
            return lower;
        }
    }

    return std::max(2, target);
}

template <int NumChannels>
std::vector<bool> AdvancedFDN<NumChannels>::buildPrimeSieve(int maxValue)
{
    std::vector<bool> sieve(static_cast<std::size_t>(maxValue) + 1u, true);
    if (maxValue >= 0)
        sieve[0] = false;
    if (maxValue >= 1)
        sieve[1] = false;

    for (int candidate = 2; candidate * candidate <= maxValue; ++candidate)
    {
        if (!sieve[static_cast<std::size_t>(candidate)])
            continue;

        for (int multiple = candidate * candidate; multiple <= maxValue; multiple += candidate)
            sieve[static_cast<std::size_t>(multiple)] = false;
    }

    return sieve;
}

template <int NumChannels>
std::array<int, NumChannels> AdvancedFDN<NumChannels>::computePrimeDelaySamples(double sampleRate)
{
    std::array<int, NumChannels> delays{};
    std::array<bool, 4096> used{};
    used.fill(false);

    const float minSamples = kMinDelayMs * static_cast<float>(sampleRate) * 0.001f;
    const float maxSamples = kMaxDelayMs * static_cast<float>(sampleRate) * 0.001f;
    const float ratio = maxSamples / minSamples;

    int maxTarget = 0;
    std::array<int, NumChannels> targets{};
    for (int i = 0; i < NumChannels; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(NumChannels - 1);
        const float ideal = minSamples * std::pow(ratio, t);
        targets[static_cast<std::size_t>(i)] = static_cast<int>(std::lround(ideal));
        maxTarget = std::max(maxTarget, targets[static_cast<std::size_t>(i)]);
    }

    const auto sieve = buildPrimeSieve(maxTarget + 64);

    for (int i = 0; i < NumChannels; ++i)
        delays[static_cast<std::size_t>(i)] = nearestAvailablePrime(targets[static_cast<std::size_t>(i)], sieve, used);

    return delays;
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);
    lfoBlockPrecomputed_ = false;

    baseDelaySamples_ = computePrimeDelaySamples(sampleRate);

    maxDelaySamples_ = 0;
    for (const int delay : baseDelaySamples_)
        maxDelaySamples_ = std::max(maxDelaySamples_, delay);

    // Size for kMaxModDepth, not the current target, so setModDepth() can reach
    // any value in [0, kMaxModDepth] without overrunning the buffer at runtime.
    const int modMargin    = static_cast<int>(std::ceil(kMaxModDepth + 2.0f));
    const int lineCapacity = maxDelaySamples_ + modMargin;

    for (auto& line : delayLines_)
        line.prepare(lineCapacity);

    constexpr float twoPi = 6.283185307179586f;
    constexpr float rateStep = 0.06f;
    constexpr float baseRate = 0.07f;

    for (int i = 0; i < NumChannels; ++i)
    {
        const float rate = baseRate + rateStep * static_cast<float>(i);
        const float phase = twoPi * static_cast<float>(i) / static_cast<float>(NumChannels);
        lfos_[static_cast<std::size_t>(i)].prepare(sampleRate, rate, phase);
    }

    injectionNorm_ = dsp::orthogonalNormalization(static_cast<std::size_t>(NumChannels));

    // ── Absorption banks (one per FDN channel) ────────────────────────────────
    for (int i = 0; i < NumChannels; ++i)
        absorptionBanks_[static_cast<std::size_t>(i)].prepare(sampleRate);

    // Compute initial decay-EQ coefficients, then immediately snap the one-pole
    // smoothers so the very first processBlock produces no transient glide from
    // identity filters to the EQ curve.
    updateFilterCoefficients();
    for (auto& bank : absorptionBanks_)
        bank.snapCoefficientsToTargets();

    constexpr float smoothingTimeSeconds = 0.05f;
    paramSmoothingCoeff_ = 1.0f - std::exp(-1.0f / (smoothingTimeSeconds * static_cast<float>(sampleRate_)));

    const float hpOmega = twoPi * kDcBlockerCutoffHz / static_cast<float>(sampleRate_);
    hpCoeff_ = 1.0f - std::exp(-hpOmega);

    feedbackCurrent_ = feedbackTarget_;
    modDepthCurrent_ = modDepthTarget_;
    dryWetCurrent_ = dryWetTarget_;

    prepared_ = true;
    reset();
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::reset() noexcept
{
    for (auto& line : delayLines_)
        line.reset();

    for (auto& lfo : lfos_)
        lfo.reset();

    for (auto& bank : absorptionBanks_)
        bank.reset();

    hpState_.fill(0.0f);
    delayed_.fill(0.0f);
    mixed_.fill(0.0f);
    lfoBlockStart_.fill(0.0f);
    lfoBlockStep_.fill(0.0f);
    lfoBlockPrecomputed_ = false;
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setFeedback(float feedback) noexcept
{
    feedbackTarget_ = std::clamp(feedback, 0.0f, 0.99f);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setModDepth(float depthSamples) noexcept
{
    modDepthTarget_ = std::clamp(depthSamples, 0.0f, kMaxModDepth);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setDryWet(float mix) noexcept
{
    dryWetTarget_ = std::clamp(mix, 0.0f, 1.0f);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setModRates(const std::array<float, NumChannels>& ratesHz) noexcept
{
    for (int i = 0; i < NumChannels; ++i)
        lfos_[static_cast<std::size_t>(i)].setFrequency(ratesHz[static_cast<std::size_t>(i)]);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::updateSmoothedParameters() noexcept
{
    feedbackCurrent_ += paramSmoothingCoeff_ * (feedbackTarget_ - feedbackCurrent_);
    modDepthCurrent_ += paramSmoothingCoeff_ * (modDepthTarget_ - modDepthCurrent_);
    dryWetCurrent_ += paramSmoothingCoeff_ * (dryWetTarget_ - dryWetCurrent_);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::precomputeLfoBlock(int numSamples) noexcept
{
    if (lfoBlockPrecomputed_)
        return;

    const float blockReciprocal = 1.0f / static_cast<float>(numSamples);
    for (int i = 0; i < NumChannels; ++i)
    {
        auto& lfo = lfos_[static_cast<std::size_t>(i)];
        const float start = lfo.getCurrentValue();
        const float end = lfo.advance(numSamples);
        lfoBlockStart_[static_cast<std::size_t>(i)] = start;
        lfoBlockStep_[static_cast<std::size_t>(i)] = (end - start) * blockReciprocal;
    }

    lfoBlockPrecomputed_ = true;
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::processBlock(const float* left,
                                            const float* right,
                                            float* outLeft,
                                            float* outRight,
                                            int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    // Guard against a DAW sending a block larger than what was prepared.
    // Scratch arrays (delayed_, mixed_) are fixed-size std::array, so there
    // is nothing to overrun here, but the caller's output pointers are sized
    // by maxBlockSize_ — do not write past them.
    if (numSamples > maxBlockSize_)
        return;

    precomputeLfoBlock(numSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        updateSmoothedParameters();

        const float dryLeft = left[sample];
        const float dryRight = right[sample];
        const float sampleIndex = static_cast<float>(sample);
        const float norm = injectionNorm_;

        for (int i = 0; i < NumChannels; ++i)
        {
            const float lfoValue = lfoBlockStart_[static_cast<std::size_t>(i)]
                                 + lfoBlockStep_[static_cast<std::size_t>(i)] * sampleIndex;
            float delay = static_cast<float>(baseDelaySamples_[static_cast<std::size_t>(i)])
                        + modDepthCurrent_ * lfoValue;
            delay = std::max(FractionalDelayLine::kMinStableDelaySamples, delay);
            delayed_[static_cast<std::size_t>(i)] = delayLines_[static_cast<std::size_t>(i)].readSample(delay);
        }

        mixed_ = delayed_;
        dsp::applyOrthogonalMix(mixed_);

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        for (int i = 0; i < NumChannels; ++i)
        {
            float sampleValue = mixed_[static_cast<std::size_t>(i)] * feedbackCurrent_;

            hpState_[static_cast<std::size_t>(i)] +=
                hpCoeff_ * (sampleValue - hpState_[static_cast<std::size_t>(i)]);
            sampleValue -= hpState_[static_cast<std::size_t>(i)];

            // ── Multi-band absorption (Phase 3: frequency-dependent decay) ───
            // Inserted AFTER DC blocker (no DC offset enters the shelf filters),
            // BEFORE dry injection (only the feedback signal is absorbed, not
            // the newly-arriving dry sound). Each channel has its own bank so
            // T60 scales correctly with each delay line's loop time.
            sampleValue = absorptionBanks_[static_cast<std::size_t>(i)].processSample(sampleValue);

            const float channelIn = ((i % 2) == 0) ? dryLeft : dryRight;
            const float injected = sampleValue + channelIn * norm;
            delayLines_[static_cast<std::size_t>(i)].writeSample(injected);

            const float tap = delayed_[static_cast<std::size_t>(i)] * norm;
            if ((i % 2) == 0)
                wetLeft += tap;
            else
                wetRight += tap;
        }

        const float wetMix = dryWetCurrent_;
        const float dryMix = 1.0f - dryWetCurrent_;
        outLeft[sample] = dryLeft * dryMix + wetLeft * wetMix;
        outRight[sample] = dryRight * dryMix + wetRight * wetMix;
    }

    lfoBlockPrecomputed_ = false;
}

// ── Phase 3: Frequency-Dependent Decay ───────────────────────────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::updateFilterCoefficients() noexcept
{
    // ── Feedback / T60 coupling (DSP tuning note) ─────────────────────────────
    // The per-loop gain at any frequency is the product of two terms:
    //
    //   loop_gain(f) = feedbackCurrent_ × absorptionBank_gain(f)
    //
    // t60ToLinearGain computes only the absorptionBank_gain component, assuming
    // feedbackCurrent_ = 1.0.  Because the actual loop gain is multiplied by
    // feedbackCurrent_ (default 0.85), the realised T60 will be SHORTER than
    // the target:
    //
    //   actual_T60 ≈ -3·D / log10(feedbackTarget_ × 10^(-3·D / targetT60))
    //
    // For typical settings (feedback=0.85, D≈10–18 ms) the discrepancy is
    // roughly 50–70 % of the target T60 (e.g. targetT60=3 s → actual≈1.5 s).
    //
    // Practical guidance
    // ─────────────────
    // • The six T60 knobs control RELATIVE (spectral shape) decay, not
    //   absolute duration.  Use setFdnFeedback() to set overall reverb length.
    // • For absolute T60 accuracy, divide the target gain by feedbackCurrent_
    //   before passing to updateFilterCoefficients, or expose a combined
    //   "decay time" + "HF/LF ratio" UI rather than raw T60 values.
    // ─────────────────────────────────────────────────────────────────────────

    for (int i = 0; i < NumChannels; ++i)
    {
        // Convert this channel's integer delay length to seconds.
        // Each channel has a unique loop time, so the per-loop attenuation
        // required to hit a target T60 differs per channel — this is what
        // gives the reverb its naturally varied spectral density.
        const float channelDelayTimeSec =
            static_cast<float>(baseDelaySamples_[static_cast<std::size_t>(i)])
            / static_cast<float>(sampleRate_);

        absorptionBanks_[static_cast<std::size_t>(i)].updateCoefficients(
            sampleRate_,
            channelDelayTimeSec,
            decayLowFreq_,  decayLowT60_,
            decayMidFreq_,  decayMidT60_,  decayMidQ_,
            decayHighFreq_, decayHighT60_);
    }
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setDecayEQ(float lowFreq,  float lowT60,
                                          float midFreq,  float midT60,
                                          float highFreq, float highT60) noexcept
{
    decayLowFreq_  = std::max(20.0f,  lowFreq);
    decayLowT60_   = std::max(0.001f, lowT60);
    decayMidFreq_  = std::max(20.0f,  midFreq);
    decayMidT60_   = std::max(0.001f, midT60);
    decayHighFreq_ = std::max(20.0f,  highFreq);
    decayHighT60_  = std::max(0.001f, highT60);

    // Recompute per-channel gains and push new coefficient targets to the
    // biquad smoothers. The audio thread picks up the changes within ~20 ms
    // (the BiquadFilter coefficient smoothing time constant).
    updateFilterCoefficients();
}
