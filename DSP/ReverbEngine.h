#pragma once

#include "EarlyReflections.h"
#include "FractionalDelayLine.h"
#include "TVFDNEngine.h"

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

    /** FDN LFO modulation depth (samples). */
    void setFdnModDepth(float d) noexcept { fdn_.setModDepth(d); }

    /** FDN output stereo width via M/S matrix (0 = mono, 1 = natural, 2 = hyper-wide). */
    void setFdnStereoWidth(float w) noexcept { fdn_.setStereoWidth(w); }

    // ── Sub-module access (diagnostics / tests) ──────────────────────────────
    const EarlyReflections& earlyReflections() const noexcept { return er_; }
    const TVFDNEngine&       fdn()             const noexcept { return fdn_; }

private:
    static constexpr float kMaxPreDelayMs = 500.0f;

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
