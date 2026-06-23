#pragma once

#include "EarlyReflections.h"
#include "FractionalDelayLine.h"
#include "TVFDNEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/**
 * Top-level reverb engine facade.
 *
 * Signal chain (decoupled ER / FDN paths)
 * ────────────────────────────────────────
 *
 *                                    ┌──► EarlyReflections ──► erOutL ──┐
 *   Dry L ──► FractionalDelayLine ──►┤                                  ├─(Distance)─► wetL
 *             (Cubic Hermite)        └──► TVFDNEngine ─────────────────►┘
 *                                         (16-ch TV-FDN, post-FWHT tap,
 *                                          M/S stereo width)
 *   (same for R)
 *
 * ER / FDN decoupling
 * ────────────────────
 * The FDN is seeded with the pre-delayed dry signal ONLY.  ER is NOT fed into
 * the FDN — doing so would cause early energy to appear twice (once in the
 * Distance crossfade and once as a re-emergent diffuse tail), blurring the
 * clean ER/tail staging that Distance is designed to control.
 *
 * Unified Reverb Time
 * ────────────────────
 * setReverbTime() drives FDN feedback + all T60 band targets in one call.
 * setDecayShape() controls spectral tilt as multipliers relative to reverbTime.
 * FDN feedback is an internal implementation detail, not exposed to the user.
 *
 * Early Reflections update
 * ─────────────────────────
 * setErLength() and setErDensity() store new values and raise erNeedsUpdate_.
 * At the top of the next processBlock() the ER is re-prepared.  This causes a
 * brief audio reset acceptable for these non-automated room-character parameters.
 *
 * Real-time constraints
 * ─────────────────────
 *   • All scratch buffers sized in prepare(); never reallocated.
 *   • processBlock() is allocation-free and lock-free (except for the one-shot
 *     ER re-prepare on a parameter change, which uses std::vector::resize).
 */
class ReverbEngine
{
public:
    ReverbEngine() = default;

    /**
     * Prepare all sub-modules.
     *
     * @param sampleRate          Host sample rate (Hz).
     * @param maxBlockSize        Maximum block size (samples).
     * @param erLengthMs          ER window (ms). Default: 80.
     * @param initialPreDelayMs   Starting pre-delay (ms, 0–500). Default: 0.
     * @param erDensityHz         ER tap density (impulses/sec). Default: 3000.
     * @param erMinSpacingMs      Minimum tap spacing (ms). Default: 1.
     * @param seedL               PRNG seed for Left ER sequence.
     * @param seedR               PRNG seed for Right ER sequence.
     */
    void prepare(double        sampleRate,
                 int           maxBlockSize,
                 float         erLengthMs           = 80.0f,
                 float         initialPreDelayMs     = 0.0f,
                 float         erDensityHz           = 3000.0f,
                 float         erMinSpacingMs        = 1.0f,
                 std::uint32_t seedL                 = 0xABCD1234u,
                 std::uint32_t seedR                 = 0x5678EF90u);

    void reset() noexcept;

    /** In-place stereo processing. left/right are read AND written. */
    void processBlock(float* left, float* right, int numSamples) noexcept;

    // ── Unified reverb time ──────────────────────────────────────────────────

    /**
     * Set perceived reverb time [0.1, 20 s].
     * Internally drives FDN feedback + all T60 band targets.
     */
    void setReverbTime(float rt) noexcept { fdn_.setReverbTime(rt); }

    /**
     * Set spectral-tilt multipliers relative to reverbTime.
     * @param bassDecayMult  lowT60  = rt × bass  [0.5, 3.0]  default 1.4
     * @param midDecayMult   midT60  = rt × mid   [0.5, 2.0]  default 1.0
     * @param hfDecayMult    highT60 = rt × hf    [0.05, 1.0] default 0.2
     */
    void setDecayShape(float bassDecayMult, float midDecayMult, float hfDecayMult) noexcept
    {
        fdn_.setDecayShape(bassDecayMult, midDecayMult, hfDecayMult);
    }

    // ── Room size & character ────────────────────────────────────────────────

    /** Scale FDN delay topology (0 = small room, 0.33 = medium, 1 = cathedral). */
    void setSize(float size) noexcept { fdn_.setSize(size); }

    /**
     * ER window length [20, 200 ms]. Triggers ER re-prepare on the next block.
     * Non-automatable (quasi-static room character parameter).
     */
    void setErLength(float ms) noexcept;

    /**
     * ER tap density [500, 8000 Hz]. Triggers ER re-prepare on the next block.
     * Non-automatable (quasi-static room character parameter).
     */
    void setErDensity(float hz) noexcept;

    // ── Global parameters (smoothed per-sample) ───────────────────────────────

    /**
     * Global pre-delay (0–500 ms).
     * Smoothed per-sample → tape-style Doppler glide on automation.
     */
    void setPreDelayMs(float ms) noexcept
    {
        preDelayMsTarget_ = std::clamp(ms, 0.0f, kMaxPreDelayMs);
    }

    /**
     * Distance (0..1) — equal-power crossfade between ER and FDN tail.
     * 0 = close (ER dominates), 1 = far (tail dominates).
     * Smoothed per-sample → zipper-free under automation.
     */
    void setDistance(float d) noexcept { distanceTarget_ = std::clamp(d, 0.0f, 1.0f); }
    float distance() const noexcept { return distanceTarget_; }

    /** Master wet level (0 = dry only, 1 = wet only). Smoothed per-sample. */
    void setMasterWet(float wet) noexcept { masterWetTarget_ = std::clamp(wet, 0.0f, 1.0f); }

    /** FDN LFO modulation depth (milliseconds). */
    void setFdnModDepth(float depthMs) noexcept { fdn_.setModDepth(depthMs); }

    /** FDN output stereo width via M/S matrix (0 = mono, 1 = natural, 2 = hyper-wide). */
    void setFdnStereoWidth(float w) noexcept { fdn_.setStereoWidth(w); }

    // ── Sub-module access (diagnostics / tests) ──────────────────────────────
    const EarlyReflections& earlyReflections() const noexcept { return er_; }
    const TVFDNEngine&       fdn()             const noexcept { return fdn_; }

private:
    static constexpr float kMaxPreDelayMs = 500.0f;

    // ── Schroeder allpass pre-diffuser ────────────────────────────────────────
    // Applied ONLY to the FDN input path (ER path is intentionally bypassed).
    // Four cascaded allpass sections per channel convert the sparse discrete
    // echoes from the FDN delay lines into a dense cloud, eliminating the
    // flutter/metallic character that is most audible at Distance = 1.
    //
    // Transfer function per stage:  H(z) = (z^{-M} - g) / (1 - g·z^{-M})
    // Cascade of 4 stages is allpass overall → spectral envelope unchanged.
    // Delay times chosen as prime-ish ms values to minimise beating.
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

    static constexpr int kNumDiffStages = 6;

    // Staggered allpass gains: early (short) stages use higher gain for dense
    // build-up; later (long) stages use lower gain to prevent distinct ringing
    // from the longest delays acting as an audible resonant comb.
    static constexpr std::array<float, kNumDiffStages> kDiffCoeffs{
        0.70f, 0.65f, 0.60f, 0.55f, 0.50f, 0.45f
    };

    // Asymmetric L/R delay sets — prime-ish values offset by ~1–3 %.
    // Identical L/R delays collapse to a mono comb filter on mono inputs;
    // the offset breaks phase correlation and widens the stereo image before
    // the signal enters the FDN.  Span 5–36 ms to match FDN delay spread.
    static constexpr std::array<float, kNumDiffStages> kDiffDelayMs_L{
        5.11f, 8.37f, 13.19f, 19.73f, 27.41f, 35.67f
    };
    static constexpr std::array<float, kNumDiffStages> kDiffDelayMs_R{
        5.23f, 8.59f, 13.43f, 20.11f, 27.83f, 36.17f
    };

    static constexpr int   kOutDiffStages = 2;
    static constexpr float kOutDiffCoeff  = 0.65f;
    static constexpr std::array<float, kOutDiffStages> kOutDiffDelayMs{
        11.3f, 23.7f
    };

    std::array<AllpassStage, kNumDiffStages> diffL_{};
    std::array<AllpassStage, kNumDiffStages> diffR_{};
    std::array<AllpassStage, kOutDiffStages> outDiffL_{};
    std::array<AllpassStage, kOutDiffStages> outDiffR_{};

    EarlyReflections    er_;
    TVFDNEngine         fdn_;

    // ── Fractional pre-delay lines ────────────────────────────────────────────
    FractionalDelayLine preDelayL_;
    FractionalDelayLine preDelayR_;

    // ── Pre-delay smoothing ───────────────────────────────────────────────────
    float preDelayMsTarget_  = 0.0f;
    float preDelayMsCurrent_ = 0.0f;
    float preDelaySmCoeff_   = 0.0f;

    // ── Distance + masterWet smoothing ───────────────────────────────────────
    float distanceTarget_      = 0.5f;
    float distanceCurrent_     = 0.5f;
    float distanceSmoothCoeff_ = 0.0f;
    float masterWetTarget_     = 1.0f;
    float masterWetCurrent_    = 1.0f;
    float masterWetSmCoeff_    = 0.0f;

    // ── Scratch buffers (sized in prepare) ───────────────────────────────────
    std::vector<float> delayedL_;
    std::vector<float> delayedR_;
    std::vector<float> erOutL_;
    std::vector<float> erOutR_;
    std::vector<float> fdnInL_;
    std::vector<float> fdnInR_;
    std::vector<float> fdnOutL_;
    std::vector<float> fdnOutR_;

    // ── ER parameters (stored for re-prepare) ────────────────────────────────
    float         erLengthMs_      = 80.0f;
    float         erDensityHz_     = 3000.0f;
    float         erMinSpacingMs_  = 1.0f;
    std::uint32_t erSeedL_         = 0xABCD1234u;
    std::uint32_t erSeedR_         = 0x5678EF90u;
    std::atomic<bool> erNeedsUpdate_{false};

    // ── Engine state ──────────────────────────────────────────────────────────
    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 0;
    bool   prepared_     = false;
};
