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
    const int modMargin     = static_cast<int>(
        std::ceil(kAllocModDepthMs * float(sampleRate_) * 0.001f)) + 4;
    const int lineCapacity  = absMaxSamples + modMargin;

    for (auto& line : delayLines_)
        line.prepare(lineCapacity);

    // ── LFOs (incommensurate rates — breaks periodic mode reinforcement) ─────
    static constexpr std::array<float, NumChannels> kLfoRateHz {
        0.113f, 0.171f, 0.227f, 0.311f, 0.387f, 0.443f, 0.521f, 0.587f,
        0.641f, 0.719f, 0.793f, 0.857f, 0.923f, 1.031f, 1.117f, 1.193f
    };

    constexpr float kGolden = 1.618033988749f;
    for (int i = 0; i < NumChannels; ++i)
    {
        lfos_[static_cast<std::size_t>(i)].prepare(
            sampleRate_,
            kLfoRateHz[static_cast<std::size_t>(i)],
            kGolden * float(i));
    }

    // ── Decorrelated stereo output panning (golden-angle spread) ─────────────
    float panEnergyL = 0.0f;
    float panEnergyR = 0.0f;
    for (int i = 0; i < NumChannels; ++i)
    {
        const float phase = kGolden * float(i);
        outPanL_[static_cast<std::size_t>(i)] = std::cos(phase);
        outPanR_[static_cast<std::size_t>(i)] = std::sin(phase);
        panEnergyL += outPanL_[static_cast<std::size_t>(i)] * outPanL_[static_cast<std::size_t>(i)];
        panEnergyR += outPanR_[static_cast<std::size_t>(i)] * outPanR_[static_cast<std::size_t>(i)];
    }
    const float panScaleL = std::sqrt(float(NumChannels) * 0.5f / std::max(panEnergyL, 1.0e-6f));
    const float panScaleR = std::sqrt(float(NumChannels) * 0.5f / std::max(panEnergyR, 1.0e-6f));
    for (int i = 0; i < NumChannels; ++i)
    {
        outPanL_[static_cast<std::size_t>(i)] *= panScaleL;
        outPanR_[static_cast<std::size_t>(i)] *= panScaleR;
    }

    // ── Normalization ─────────────────────────────────────────────────────────
    injectionNorm_ = dsp::orthogonalNormalization(static_cast<std::size_t>(NumChannels));

    // ── Absorption banks ──────────────────────────────────────────────────────
    for (auto& bank : absorptionBanks_)
        bank.prepare(sampleRate_);

    // ── Smoothing coefficients ─────────────────────────────────────────────────
    constexpr float kSmTimeS = 0.05f; // 50 ms time constant
    paramSmoothingCoeff_ = 1.0f - std::exp(-1.0f / (kSmTimeS * float(sampleRate_)));

    // Delay-target smoother: 300 ms TC → natural Doppler sweep on size change.
    constexpr float kDelaySmTimeS = 0.30f;
    delaySmoothCoeff_ = 1.0f - std::exp(-1.0f / (kDelaySmTimeS * float(sampleRate_)));

    // Snap delay targets to initial base delays (no sweep on first block)
    for (int i = 0; i < NumChannels; ++i)
        smoothedDelayTargets_[static_cast<std::size_t>(i)] =
            float(baseDelaySamples_[static_cast<std::size_t>(i)]);

    const float hpOmega = 6.283185307179586f * kDcBlockerCutoffHz / float(sampleRate_);
    hpCoeff_ = 1.0f - std::exp(-hpOmega);

    // ── Randomized signed-Hadamard sign matrices ──────────────────────────────
    // Fixed-seed xorshift32 ensures deterministic behaviour across sessions.
    // Pre-signs and post-signs are generated from independent RNG streams so
    // the two mixing passes have uncorrelated sign patterns.
    {
        std::uint32_t rng = 0xDEADBEEFu;
        for (int i = 0; i < NumChannels; ++i)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            fwhtPreSigns_[static_cast<std::size_t>(i)] = (rng & 1u) ? 1.0f : -1.0f;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            fwhtPostSigns_[static_cast<std::size_t>(i)] = (rng & 1u) ? 1.0f : -1.0f;
        }
    }

    // ── Initial RT + decay EQ coefficients ────────────────────────────────────
    // setReverbTime computes feedbackTarget_ and T60 bands, then calls
    // updateFilterCoefficients (which requires prepared_ = true, so set it first).
    prepared_ = true;
    setReverbTime(reverbTimeSec_); // feeds feedbackTarget_, channelGainTarget_, filter coeffs

    // Snap smoothers to targets — no glide on first processBlock
    feedbackCurrent_    = feedbackTarget_;
    modDepthCurrentMs_  = modDepthTargetMs_;
    dryWetCurrent_      = dryWetTarget_;
    stereoWidthCurrent_ = stereoWidthTarget_;

    // Snap per-channel gains — prevent a sweep from 0 to target on first block
    for (int i = 0; i < NumChannels; ++i)
        channelGainCurrent_[static_cast<std::size_t>(i)] =
            channelGainTarget_[static_cast<std::size_t>(i)];

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
    filtered_.fill(0.0f);
    lfoBlockStart_.fill(0.0f);
    lfoBlockStep_.fill(0.0f);
    lfoBlockPrecomputed_ = false;

    // Snap delay smoothers after a hard reset so no sweep occurs from silence
    for (int i = 0; i < NumChannels; ++i)
    {
        smoothedDelayTargets_[static_cast<std::size_t>(i)] =
            float(baseDelaySamples_[static_cast<std::size_t>(i)]);
        // Snap channel gains too — prevent a ramp from 0 after a tail reset
        channelGainCurrent_[static_cast<std::size_t>(i)] =
            channelGainTarget_[static_cast<std::size_t>(i)];
    }
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

    // ── Per-channel loop gains targeting T60_bass (longest band) ─────────────
    // Using T60_bass as the base means the absorption banks only ever need to
    // ATTENUATE (never boost), so all loop gains stay safely below 1.0.
    if (prepared_)
    {
        for (int i = 0; i < NumChannels; ++i)
        {
            const float D = float(baseDelaySamples_[static_cast<std::size_t>(i)])
                          / float(sampleRate_);
            if (D > 0.0f)
            {
                channelGainTarget_[static_cast<std::size_t>(i)] = std::clamp(
                    std::pow(10.0f, -3.0f * D / decayLowT60_),
                    0.01f, 0.9999f);
            }
        }
        updateFilterCoefficients();
    }
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

    // ── Click-free transition: NO hard reset of delay lines ───────────────────
    // smoothedDelayTargets_[i] will glide toward the new baseDelaySamples_[i]
    // values over ~300 ms (see delaySmoothCoeff_), producing a natural Doppler
    // sweep.  The delay-line buffers are pre-sized for kAbsMaxDelayMs so any
    // read position within the new size range is already valid.
    //
    // We only recompute:
    //   • feedbackTarget_  (avgDelay changes → different RT math)
    //   • filter coefficients  (absorption bands use channel delay time)
    // Let feedbackCurrent_ smooth toward the new feedbackTarget_ per-sample
    // (the per-sample smoother in updateSmoothedParameters handles this).

    const float avgDelayS = computeAvgDelaySec();
    if (avgDelayS > 0.0f && reverbTimeSec_ > 0.0f)
    {
        feedbackTarget_ = std::pow(10.0f, -3.0f * avgDelayS / reverbTimeSec_);
        feedbackTarget_ = std::clamp(feedbackTarget_, 0.01f, 0.99f);
        // Do NOT snap feedbackCurrent_ here — let it smooth naturally.
    }

    // Recompute per-channel gains for the new delay topology.
    // decayLowT60_ was set by the last setReverbTime call and is still valid.
    for (int i = 0; i < NumChannels; ++i)
    {
        const float D = float(baseDelaySamples_[static_cast<std::size_t>(i)])
                      / float(sampleRate_);
        if (D > 0.0f)
        {
            channelGainTarget_[static_cast<std::size_t>(i)] = std::clamp(
                std::pow(10.0f, -3.0f * D / decayLowT60_),
                0.01f, 0.9999f);
        }
        // Do NOT snap channelGainCurrent_ — let it glide naturally.
    }

    updateFilterCoefficients();
    // Coefficients are already smoothed per-sample by BiquadFilter's own smoothers,
    // so no snapCoefficientsToTargets() call is needed here.

    sizeNeedsUpdate_.store(false, std::memory_order_release);
}

// ── Modulation setters ─────────────────────────────────────────────────────────

template <int NumChannels>
void AdvancedFDN<NumChannels>::setModDepth(float depthMs) noexcept
{
    modDepthTargetMs_ = std::clamp(depthMs, 0.0f, kMaxModDepthMs);
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
    modDepthCurrentMs_  += paramSmoothingCoeff_ * (modDepthTargetMs_  - modDepthCurrentMs_);
    dryWetCurrent_      += paramSmoothingCoeff_ * (dryWetTarget_      - dryWetCurrent_);
    stereoWidthCurrent_ += paramSmoothingCoeff_ * (stereoWidthTarget_ - stereoWidthCurrent_);

    // Smooth per-channel loop gains (same 50 ms time constant).
    // Prevents zipper noise when RT or room size changes cause targets to jump.
    for (int i = 0; i < NumChannels; ++i)
        channelGainCurrent_[static_cast<std::size_t>(i)] +=
            paramSmoothingCoeff_ * (channelGainTarget_[static_cast<std::size_t>(i)]
                                  - channelGainCurrent_[static_cast<std::size_t>(i)]);
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
    // ── Per-channel, per-frequency decay — FabFilter/Valhalla approach ────────
    //
    // channelGainTarget_[i] already handles the flat (bass) loop gain for
    // channel i:  g_base(i) = 10^(−3·D_i / T60_bass).
    //
    // The absorption bank needs to supply only the DIFFERENTIAL attenuation
    // that makes mid and HF decay faster than bass.  The effective T60 seen
    // by the absorption bank (relative to the bass base) is:
    //
    //   T60_eff(band) = 1 / (1/T60_band − 1/T60_bass)
    //
    // Because T60_band ≤ T60_bass for mid and HF, this is always positive and
    // the corresponding absorption gain is always ≤ 1 — no pass-through, no
    // unstable boosts.  Every channel at every frequency now hits its T60
    // target exactly, regardless of individual delay length.

    for (int i = 0; i < NumChannels; ++i)
    {
        const float D = float(baseDelaySamples_[static_cast<std::size_t>(i)])
                      / float(sampleRate_);

        // Converts an absolute T60 band target into the effective T60 that the
        // absorption bank must achieve, given that the channel base gain already
        // handles T60_bass.  Returns a large sentinel when the band target equals
        // or exceeds T60_bass (i.e., no additional attenuation required).
        auto effectiveT60 = [&](float t60Band) -> float
        {
            if (t60Band <= 0.0f) return 0.001f;
            const float invDiff = (1.0f / t60Band) - (1.0f / decayLowT60_);
            if (invDiff < 1e-7f) return 1000.0f;   // band ≥ bass → near pass-through
            return 1.0f / invDiff;
        };

        absorptionBanks_[static_cast<std::size_t>(i)].updateCoefficients(
            sampleRate_, D,
            kDecayLowFreqHz,  effectiveT60(decayLowT60_),    // ≈ 1000 s → gain ≈ 1
            kDecayMidFreqHz,  effectiveT60(decayMidT60_),  kDecayMidQ,
            kDecayHighFreqHz, effectiveT60(decayHighT60_));
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

        const float dryLeft         = left[sample];
        const float dryRight        = right[sample];
        const float sampleIdx       = float(sample);
        const float norm            = injectionNorm_;
        const float modDepthSamples = modDepthCurrentMs_
                                    * static_cast<float>(sampleRate_) * 0.001f;
        // Mono seed into every delay line — alternating L/R injection creates
        // static stereo comb modes that ring in the pure-tail (Distance=1) path.
        const float monoIn          = (dryLeft + dryRight) * 0.5f;

        // ── Advance per-channel delay smoothers ───────────────────────────────
        // Each smoothedDelayTargets_[i] tracks baseDelaySamples_[i] with a
        // ~300 ms one-pole filter.  Using the smoothed value instead of the
        // raw integer target produces a click-free Doppler sweep when size
        // changes, rather than an abrupt jump in read position.
        for (int i = 0; i < NumChannels; ++i)
        {
            const float target = float(baseDelaySamples_[static_cast<std::size_t>(i)]);
            smoothedDelayTargets_[static_cast<std::size_t>(i)] +=
                delaySmoothCoeff_ * (target - smoothedDelayTargets_[static_cast<std::size_t>(i)]);
        }

        // ── Read all delay lines ──────────────────────────────────────────────
        for (int i = 0; i < NumChannels; ++i)
        {
            const float lfoValue = lfoBlockStart_[static_cast<std::size_t>(i)]
                                 + lfoBlockStep_ [static_cast<std::size_t>(i)] * sampleIdx;

            // Clamp modulation depth to 25 % of the channel's base delay.
            // Without this, short channels (5–8 ms) with the default 4 ms mod
            // depth receive 50–80 % amplitude modulation — audible chorus /
            // pitch-wobble that reads as metallic twang.
            const float baseDelay = smoothedDelayTargets_[static_cast<std::size_t>(i)];
            const float maxMod    = baseDelay * 0.25f;
            const float clampedMod = std::min(modDepthSamples, maxMod);

            float delay = baseDelay + clampedMod * lfoValue;
            delay = std::max(FractionalDelayLine::kMinStableDelaySamples, delay);
            delayed_[static_cast<std::size_t>(i)] =
                delayLines_[static_cast<std::size_t>(i)].readSample(delay);
        }

        // ── Signed-Hadamard feedback mix ─────────────────────────────────────
        // Pre-multiply by random ±1 signs before the FWHT.  The effective
        // feedback matrix becomes D_pre × FWHT, which is orthogonal but has
        // no regular structure — modal resonances are spread uniformly rather
        // than concentrating in the Hadamard eigenmodes.
        for (int i = 0; i < NumChannels; ++i)
            mixed_[static_cast<std::size_t>(i)] = delayed_[static_cast<std::size_t>(i)]
                                                * fwhtPreSigns_[static_cast<std::size_t>(i)];
        dsp::applyOrthogonalMix(mixed_);

        // ── Feedback loop: per-channel gain → DC block → absorption → write ─────
        // channelGainCurrent_[i] = 10^(−3·D_i / T60_bass) independently for
        // each channel, replacing the old global feedbackCurrent_ scalar.
        // This ensures every delay line achieves the correct T60 regardless
        // of its length — the primary fix for modal metallic ringing.
        for (int i = 0; i < NumChannels; ++i)
        {
            float sampleValue = mixed_[static_cast<std::size_t>(i)]
                              * channelGainCurrent_[static_cast<std::size_t>(i)];

            // DC blocker (one-pole HP at ~5 Hz) — prevents LF drift into shelves
            hpState_[static_cast<std::size_t>(i)] +=
                hpCoeff_ * (sampleValue - hpState_[static_cast<std::size_t>(i)]);
            sampleValue -= hpState_[static_cast<std::size_t>(i)];

            // Multi-band absorption — AFTER DC blocker, BEFORE dry injection.
            sampleValue = absorptionBanks_[static_cast<std::size_t>(i)].processSample(sampleValue);
            filtered_[static_cast<std::size_t>(i)] = sampleValue;

            // ── Single-channel input injection ───────────────────────────────
            // Inject only into channel 0.  A single-point B vector distributes
            // input energy uniformly across ALL 16 Hadamard modes after the
            // next FWHT pass — instead of concentrating all energy in the DC
            // (all-ones) mode as the old equal all-channel injection did.
            // Total energy unchanged: monoIn² = 16 × (monoIn × 0.25)² (same).
            const float injectAmt = (i == 0) ? monoIn : 0.0f;
            delayLines_[static_cast<std::size_t>(i)].writeSample(sampleValue + injectAmt);
        }

        // ── Signed-Hadamard output mix ───────────────────────────────────────
        // Apply a second (independent) set of ±1 signs before the output FWHT.
        // Using different signs from the feedback mix ensures the two FWHT
        // passes create orthogonally-rotated output bases, maximising
        // left/right decorrelation in the stereo pan accumulation below.
        for (int i = 0; i < NumChannels; ++i)
            filtered_[static_cast<std::size_t>(i)] *= fwhtPostSigns_[static_cast<std::size_t>(i)];
        dsp::applyOrthogonalMix(filtered_);

        float wetLeft  = 0.0f;
        float wetRight = 0.0f;
        for (int i = 0; i < NumChannels; ++i)
        {
            const float tap = filtered_[static_cast<std::size_t>(i)] * norm;
            wetLeft  += tap * outPanL_[static_cast<std::size_t>(i)];
            wetRight += tap * outPanR_[static_cast<std::size_t>(i)];
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
