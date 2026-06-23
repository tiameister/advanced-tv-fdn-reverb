#include "AdvancedFDN.h"

#include <algorithm>
#include <numeric>

// ── Prime delay helpers ────────────────────────────────────────────────────────

template <int NumChannels>
int AdvancedFDN<NumChannels>::nearestAvailablePrime(
    int target,
    const std::vector<bool>& isPrimeTable,
    std::array<bool, 4096>& used) noexcept
{
    const int maxIndex = static_cast<int>(isPrimeTable.size()) - 1;

    for (int offset = 0; offset <= maxIndex; ++offset)
    {
        const int upper = target + offset;
        if (upper <= maxIndex
            && isPrimeTable[static_cast<std::size_t>(upper)]
            && !used[static_cast<std::size_t>(upper)])
        {
            used[static_cast<std::size_t>(upper)] = true;
            return upper;
        }

        const int lower = target - offset;
        if (lower >= 2
            && isPrimeTable[static_cast<std::size_t>(lower)]
            && !used[static_cast<std::size_t>(lower)])
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
    if (maxValue >= 0) sieve[0] = false;
    if (maxValue >= 1) sieve[1] = false;

    for (int c = 2; c * c <= maxValue; ++c)
    {
        if (!sieve[static_cast<std::size_t>(c)])
            continue;
        for (int m = c * c; m <= maxValue; m += c)
            sieve[static_cast<std::size_t>(m)] = false;
    }

    return sieve;
}

template <int NumChannels>
std::array<int, NumChannels>
AdvancedFDN<NumChannels>::computePrimeDelaySamples(double sampleRate,
                                                    float  minMs,
                                                    float  maxMs)
{
    std::array<int, NumChannels> delays{};
    std::array<bool, 4096> used{};
    used.fill(false);

    const float minS = minMs * static_cast<float>(sampleRate) * 0.001f;
    const float maxS = maxMs * static_cast<float>(sampleRate) * 0.001f;
    const float ratio = maxS / std::max(minS, 1.0f);

    int maxTarget = 0;
    std::array<int, NumChannels> targets{};
    for (int i = 0; i < NumChannels; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(NumChannels - 1);
        const float ideal = minS * std::pow(ratio, t);
        targets[static_cast<std::size_t>(i)] = static_cast<int>(std::lround(ideal));
        maxTarget = std::max(maxTarget, targets[static_cast<std::size_t>(i)]);
    }

    const auto sieve = buildPrimeSieve(maxTarget + 64);

    for (int i = 0; i < NumChannels; ++i)
        delays[static_cast<std::size_t>(i)] =
            nearestAvailablePrime(targets[static_cast<std::size_t>(i)], sieve, used);

    return delays;
}

// ── Size helpers ───────────────────────────────────────────────────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::getSizeRange(float size, float& minMs, float& maxMs) noexcept
{
    // Log-linear interpolation:
    //   size=0  → [  2,  8] ms  small room
    //   size≈0.33 → [3, 18] ms  medium (matches default primes)
    //   size=1  → [ 15, 80] ms  cathedral
    const float t = std::clamp(size, 0.0f, 1.0f);
    minMs = 2.0f  * std::pow(7.5f,  t);  // 2 → 15 ms
    maxMs = 8.0f  * std::pow(10.0f, t);  // 8 → 80 ms
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::scaleDelaysFromPrimes(float minMs, float maxMs) noexcept
{
    // Map each prime delay (computed for [kDefaultMinMs, kDefaultMaxMs]) into
    // the new [minMs, maxMs] range by preserving the normalized log-position t:
    //   t = (origMs - defaultMin) / defaultRange  →  scaledMs = minMs + t × newRange
    const float defaultMin   = kDefaultMinDelayMs;
    const float defaultRange = kDefaultMaxDelayMs - kDefaultMinDelayMs;
    const float newRange     = maxMs - minMs;
    const float srInv        = 1.0f / (float(sampleRate_) * 0.001f);

    for (int i = 0; i < NumChannels; ++i)
    {
        const float origMs  = float(primeBaseDelaySamples_[static_cast<std::size_t>(i)])
                            * srInv;
        const float t       = (origMs - defaultMin) / defaultRange;
        const float scaled  = std::clamp(minMs + t * newRange, 2.0f, kAbsMaxDelayMs);
        baseDelaySamples_[static_cast<std::size_t>(i)] =
            std::max(1, int(std::round(scaled / srInv)));
    }
}

// ── Lifecycle ──────────────────────────────────────────────────────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);
    lfoBlockPrecomputed_ = false;

    // ── Compute prime delays for the default size ─────────────────────────────
    // Stored as the canonical template; setSize() rescales from this.
    primeBaseDelaySamples_ = computePrimeDelaySamples(sampleRate_);

    // Apply initial size (may not be default if setSize was called before prepare)
    {
        float sizeMinMs, sizeMaxMs;
        getSizeRange(sizeTarget_, sizeMinMs, sizeMaxMs);
        scaleDelaysFromPrimes(sizeMinMs, sizeMaxMs);
    }

    // ── Allocate delay lines for kAbsMaxDelayMs ───────────────────────────────
    // Always allocate for the absolute maximum so setSize() never needs to
    // reallocate — it only changes which portion of the buffer is used.
    const int absMaxSamples =
        static_cast<int>(kAbsMaxDelayMs * float(sampleRate_) * 0.001f) + 1;
    const int modMargin     = static_cast<int>(std::ceil(kMaxModDepth + 2.0f));
    const int lineCapacity  = absMaxSamples + modMargin;

    for (auto& line : delayLines_)
        line.prepare(lineCapacity);

    // ── LFOs ──────────────────────────────────────────────────────────────────
    constexpr float twoPi    = 6.283185307179586f;
    constexpr float rateStep = 0.06f;
    constexpr float baseRate = 0.07f;

    for (int i = 0; i < NumChannels; ++i)
    {
        const float rate  = baseRate + rateStep * float(i);
        const float phase = twoPi * float(i) / float(NumChannels);
        lfos_[static_cast<std::size_t>(i)].prepare(sampleRate_, rate, phase);
    }

    // ── Normalization ─────────────────────────────────────────────────────────
    injectionNorm_ = dsp::orthogonalNormalization(static_cast<std::size_t>(NumChannels));

    // ── Absorption banks ──────────────────────────────────────────────────────
    for (auto& bank : absorptionBanks_)
        bank.prepare(sampleRate_);

    // ── Smoothing coefficients ─────────────────────────────────────────────────
    constexpr float kSmTimeS = 0.05f; // 50 ms time constant
    paramSmoothingCoeff_ = 1.0f - std::exp(-1.0f / (kSmTimeS * float(sampleRate_)));

    const float hpOmega = twoPi * kDcBlockerCutoffHz / float(sampleRate_);
    hpCoeff_ = 1.0f - std::exp(-hpOmega);

    // ── Initial RT + decay EQ coefficients ────────────────────────────────────
    // setReverbTime computes feedbackTarget_ and T60 bands, then calls
    // updateFilterCoefficients (which requires prepared_ = true, so set it first).
    prepared_ = true;
    setReverbTime(reverbTimeSec_); // feeds feedbackTarget_ + filter coeffs

    // Snap smoothers to targets — no glide on first processBlock
    feedbackCurrent_    = feedbackTarget_;
    modDepthCurrent_    = modDepthTarget_;
    dryWetCurrent_      = dryWetTarget_;
    stereoWidthCurrent_ = stereoWidthTarget_;

    for (auto& bank : absorptionBanks_)
        bank.snapCoefficientsToTargets();

    sizeNeedsUpdate_.store(false, std::memory_order_relaxed);

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

// ── Unified reverb time ────────────────────────────────────────────────────────

template <int NumChannels>
float AdvancedFDN<NumChannels>::computeAvgDelaySec() const noexcept
{
    float sum = 0.0f;
    for (const int d : baseDelaySamples_)
        sum += float(d);
    return (sum / float(NumChannels)) / float(sampleRate_);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setReverbTime(float reverbTimeSec) noexcept
{
    reverbTimeSec_ = std::clamp(reverbTimeSec, 0.1f, 20.0f);

    // ── Derive per-band T60 targets ───────────────────────────────────────────
    decayLowT60_  = reverbTimeSec_ * bassDecayMult_;
    decayMidT60_  = reverbTimeSec_ * midDecayMult_;
    decayHighT60_ = reverbTimeSec_ * hfDecayMult_;

    // ── Compute feedback from RT and current average delay ────────────────────
    // feedback = 10^(−3 × avgDelayS / RT) produces −60 dB at ≈ RT seconds
    // for the DC component of the loop gain. The absorption banks then shape
    // the spectral tilt around this base decay rate.
    const float avgDelayS = computeAvgDelaySec();
    if (avgDelayS > 0.0f)
    {
        feedbackTarget_ = std::pow(10.0f, -3.0f * avgDelayS / reverbTimeSec_);
        feedbackTarget_ = std::clamp(feedbackTarget_, 0.01f, 0.99f);
    }

    if (prepared_)
        updateFilterCoefficients();
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setDecayShape(float bassDecayMult,
                                              float midDecayMult,
                                              float hfDecayMult) noexcept
{
    bassDecayMult_ = std::clamp(bassDecayMult, 0.5f, 3.0f);
    midDecayMult_  = std::clamp(midDecayMult,  0.5f, 2.0f);
    hfDecayMult_   = std::clamp(hfDecayMult,   0.05f, 1.0f);

    // Re-derive T60 bands from current RT (decayLowT60_ etc. are updated here)
    setReverbTime(reverbTimeSec_);
}

// ── Room size ──────────────────────────────────────────────────────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::setSize(float size) noexcept
{
    sizeTarget_ = std::clamp(size, 0.0f, 1.0f);
    if (prepared_)
        sizeNeedsUpdate_.store(true, std::memory_order_release);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::applySizePendingUpdate() noexcept
{
    float sizeMinMs, sizeMaxMs;
    getSizeRange(sizeTarget_, sizeMinMs, sizeMaxMs);
    scaleDelaysFromPrimes(sizeMinMs, sizeMaxMs);

    // Reset delay lines — brief gap, acceptable for a room-mode change
    for (auto& line : delayLines_)
        line.reset();
    for (auto& bank : absorptionBanks_)
        bank.reset();
    hpState_.fill(0.0f);
    delayed_.fill(0.0f);
    mixed_.fill(0.0f);

    // Re-derive feedback since avgDelay changed with the new size
    const float avgDelayS = computeAvgDelaySec();
    if (avgDelayS > 0.0f && reverbTimeSec_ > 0.0f)
    {
        feedbackTarget_  = std::pow(10.0f, -3.0f * avgDelayS / reverbTimeSec_);
        feedbackTarget_  = std::clamp(feedbackTarget_, 0.01f, 0.99f);
        feedbackCurrent_ = feedbackTarget_; // snap — avoid glide from old value
    }

    updateFilterCoefficients();
    for (auto& bank : absorptionBanks_)
        bank.snapCoefficientsToTargets();

    sizeNeedsUpdate_.store(false, std::memory_order_release);
}

// ── Modulation setters ─────────────────────────────────────────────────────────

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
void AdvancedFDN<NumChannels>::setStereoWidth(float width) noexcept
{
    stereoWidthTarget_ = std::clamp(width, 0.0f, 2.0f);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::setModRates(const std::array<float, NumChannels>& ratesHz) noexcept
{
    for (int i = 0; i < NumChannels; ++i)
        lfos_[static_cast<std::size_t>(i)].setFrequency(ratesHz[static_cast<std::size_t>(i)]);
}

// ── Internal smoothing ─────────────────────────────────────────────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::updateSmoothedParameters() noexcept
{
    feedbackCurrent_    += paramSmoothingCoeff_ * (feedbackTarget_    - feedbackCurrent_);
    modDepthCurrent_    += paramSmoothingCoeff_ * (modDepthTarget_    - modDepthCurrent_);
    dryWetCurrent_      += paramSmoothingCoeff_ * (dryWetTarget_      - dryWetCurrent_);
    stereoWidthCurrent_ += paramSmoothingCoeff_ * (stereoWidthTarget_ - stereoWidthCurrent_);
}

template <int NumChannels>
void AdvancedFDN<NumChannels>::precomputeLfoBlock(int numSamples) noexcept
{
    if (lfoBlockPrecomputed_)
        return;

    const float blockReciprocal = 1.0f / float(numSamples);
    for (int i = 0; i < NumChannels; ++i)
    {
        auto& lfo          = lfos_[static_cast<std::size_t>(i)];
        const float start  = lfo.getCurrentValue();
        const float end    = lfo.advance(numSamples);
        lfoBlockStart_[static_cast<std::size_t>(i)] = start;
        lfoBlockStep_ [static_cast<std::size_t>(i)] = (end - start) * blockReciprocal;
    }

    lfoBlockPrecomputed_ = true;
}

// ── Phase 3: Frequency-Dependent Decay (feedback-compensated) ─────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::updateFilterCoefficients() noexcept
{
    // ── Feedback / T60 coupling fix ───────────────────────────────────────────
    // The combined per-loop gain at frequency f is:
    //   g_total(f) = feedbackTarget_ × absorption_gain(f)
    //             = 10^(−3D / T60(f))   [desired]
    //
    // Solving for absorption_gain:
    //   absorption_gain(f) = 10^(−3D / T60(f)) / feedbackTarget_
    //
    // If absorption_gain ≥ 1 (the desired T60 is longer than feedback alone
    // achieves at this channel/band), the bank passes through with no attenuation
    // and the actual RT will be slightly shorter than the target. This is
    // physically correct — you cannot lengthen a reverb by un-absorbing.
    //
    // We convert this compensated gain back to an effective T60 and pass that
    // to AbsorptionBank::updateCoefficients, which then designs RBJ shelf/peak
    // coefficients for that T60. The AbsorptionBank API is unchanged.

    const float fb = std::max(feedbackTarget_, 0.001f);  // guard against log(0)

    for (int i = 0; i < NumChannels; ++i)
    {
        const float D = float(baseDelaySamples_[static_cast<std::size_t>(i)])
                      / float(sampleRate_);

        // Returns the absorption-bank T60 such that feedback × absorption hits
        // the target loop gain at each frequency.
        auto compensatedT60 = [&](float t60Target) -> float
        {
            if (t60Target <= 0.0f) return 0.001f;
            const float gTotal = std::pow(10.0f, -3.0f * D / t60Target);
            const float gAbs   = gTotal / fb;
            if (gAbs >= 1.0f)
                return 1000.0f; // no absorption needed; pass-through for this band
            return -3.0f * D / std::log10(gAbs);
        };

        absorptionBanks_[static_cast<std::size_t>(i)].updateCoefficients(
            sampleRate_, D,
            kDecayLowFreqHz,  compensatedT60(decayLowT60_),
            kDecayMidFreqHz,  compensatedT60(decayMidT60_),  kDecayMidQ,
            kDecayHighFreqHz, compensatedT60(decayHighT60_));
    }
}

// ── processBlock ──────────────────────────────────────────────────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::processBlock(const float* left,
                                            const float* right,
                                            float* outLeft,
                                            float* outRight,
                                            int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    if (numSamples > maxBlockSize_)
        return;

    // ── Apply pending size change (allocation-free, brief reset) ─────────────
    if (sizeNeedsUpdate_.load(std::memory_order_acquire))
        applySizePendingUpdate();

    precomputeLfoBlock(numSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        updateSmoothedParameters();

        const float dryLeft    = left[sample];
        const float dryRight   = right[sample];
        const float sampleIdx  = float(sample);
        const float norm       = injectionNorm_;

        // ── Read all delay lines ──────────────────────────────────────────────
        for (int i = 0; i < NumChannels; ++i)
        {
            const float lfoValue = lfoBlockStart_[static_cast<std::size_t>(i)]
                                 + lfoBlockStep_ [static_cast<std::size_t>(i)] * sampleIdx;
            float delay = float(baseDelaySamples_[static_cast<std::size_t>(i)])
                        + modDepthCurrent_ * lfoValue;
            delay = std::max(FractionalDelayLine::kMinStableDelaySamples, delay);
            delayed_[static_cast<std::size_t>(i)] =
                delayLines_[static_cast<std::size_t>(i)].readSample(delay);
        }

        // ── FWHT orthogonal mix ───────────────────────────────────────────────
        mixed_ = delayed_;
        dsp::applyOrthogonalMix(mixed_);

        // ── Feedback loop: DC block → absorption → inject → write ─────────────
        float wetLeft  = 0.0f;
        float wetRight = 0.0f;

        for (int i = 0; i < NumChannels; ++i)
        {
            float sampleValue = mixed_[static_cast<std::size_t>(i)] * feedbackCurrent_;

            // DC blocker (one-pole HP at ~5 Hz) — prevents LF drift into shelves
            hpState_[static_cast<std::size_t>(i)] +=
                hpCoeff_ * (sampleValue - hpState_[static_cast<std::size_t>(i)]);
            sampleValue -= hpState_[static_cast<std::size_t>(i)];

            // Multi-band absorption — AFTER DC blocker, BEFORE dry injection.
            // Only the recirculating signal is absorbed, not the newly-arriving input.
            sampleValue = absorptionBanks_[static_cast<std::size_t>(i)].processSample(sampleValue);

            const float channelIn = ((i % 2) == 0) ? dryLeft : dryRight;
            const float injected  = sampleValue + channelIn * norm;
            delayLines_[static_cast<std::size_t>(i)].writeSample(injected);

            // ── Post-FWHT output tap ──────────────────────────────────────────
            // Tap from mixed_[i] (post-FWHT diffuse field) not delayed_[i].
            // Each output sample is a normalised blend of ALL 16 delay channels
            // → enveloping, wrap-around spatial field.
            // Level is preserved: mixed_[i] × norm ≈ delayed_[i] × norm for
            // uncorrelated channels (FWHT energy-normalisation cancels exactly).
            const float tap = mixed_[static_cast<std::size_t>(i)] * norm;
            if ((i % 2) == 0)
                wetLeft  += tap;
            else
                wetRight += tap;
        }

        // ── M/S stereo width ─────────────────────────────────────────────────
        // At width=1 → identity. At width=0 → mono. At width=2 → hyper-wide.
        {
            const float mid  = (wetLeft + wetRight) * 0.5f;
            const float side = (wetLeft - wetRight) * (0.5f * stereoWidthCurrent_);
            wetLeft  = mid + side;
            wetRight = mid - side;
        }

        const float wetMix = dryWetCurrent_;
        const float dryMix = 1.0f - dryWetCurrent_;
        outLeft [sample] = dryLeft  * dryMix + wetLeft  * wetMix;
        outRight[sample] = dryRight * dryMix + wetRight * wetMix;
    }

    lfoBlockPrecomputed_ = false;
}
