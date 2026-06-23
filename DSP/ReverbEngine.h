#pragma once

#include "EarlyReflections.h"
#include "TVFDNEngine.h"

#include <cstdint>
#include <vector>

/**
 * Top-level reverb engine facade (Phase 1 + Phase 2).
 *
 * Signal chain
 * ────────────
 *
 *   Dry L ──────────────────────────────────────────────────────────────────┐
 *           │                                                                │
 *           └──► PreDelayRing ──► delayedL ──┬──► EarlyReflections ──► erOutL ──► erGain ──┐
 *                                             │                                              + ──► wetL ──► out
 *                                             └──► FDN input ──► TVFDNEngine ──► fdnOutL ──► tailGain ─┘
 *
 *   (same topology for R)
 *
 * Key invariant: the undelayed dry signal feeds ONLY the output mix (dry
 * path). Both the ER and the FDN tank are driven by the same pre-delayed
 * signal, so the tail and reflections always start at the same moment in time.
 *
 * Distance parameter (equal-power crossfade)
 * ──────────────────────────────────────────
 *   erGain   = cos(distance * π/2)   →  1 at distance=0, 0 at distance=1
 *   tailGain = sin(distance * π/2)   →  0 at distance=0, 1 at distance=1
 *
 * cos/sin are computed once in setDistance() and cached — zero transcendental
 * calls in the audio thread.
 *
 * Real-time constraints
 * ─────────────────────
 *   • All scratch buffers and rings sized in prepare(); never reallocated.
 *   • processBlock() is allocation-free and lock-free.
 */
class ReverbEngine
{
public:
    ReverbEngine() = default;

    /**
     * Prepare all sub-modules.
     *
     * @param sampleRate      Host sample rate (Hz).
     * @param maxBlockSize    Maximum block size (samples).
     * @param erLengthMs      ER window (ms). Default: 80.
     * @param preDelayMs      Global pre-delay applied to both ER and FDN (ms, 0–500).
     * @param erDensityHz     ER tap density (impulses/sec). Default: 3000.
     * @param erMinSpacingMs  Minimum tap spacing (ms). Default: 1.
     * @param seedL           PRNG seed for Left ER sequence.
     * @param seedR           PRNG seed for Right ER sequence.
     */
    void prepare(double        sampleRate,
                 int           maxBlockSize,
                 float         erLengthMs      = 80.0f,
                 float         preDelayMs      = 0.0f,
                 float         erDensityHz     = 3000.0f,
                 float         erMinSpacingMs  = 1.0f,
                 std::uint32_t seedL           = 0xABCD1234u,
                 std::uint32_t seedR           = 0x5678EF90u);

    void reset() noexcept;

    /** In-place stereo processing. left/right are read AND written. */
    void processBlock(float* left, float* right, int numSamples) noexcept;

    // ── Parameter setters ────────────────────────────────────────────────────

    /**
     * Distance (0..1) — equal-power crossfade between ER and FDN tail.
     * 0 = close (ER only), 0.5 = balanced, 1 = far (tail only).
     * Caches cos/sin gains immediately; safe to call from any thread before
     * the next processBlock.
     */
    void setDistance(float d) noexcept;
    float distance()          const noexcept { return distance_; }

    /** Master wet level: 0 = dry only, 1 = wet only. */
    void setMasterWet(float wet) noexcept { masterWet_ = std::clamp(wet, 0.0f, 1.0f); }

    /** FDN feedback (0..0.99). */
    void setFdnFeedback(float g) noexcept { fdn_.setFeedback(g); }

    /** FDN LFO modulation depth (samples). */
    void setFdnModDepth(float d) noexcept { fdn_.setModDepth(d); }

    // ── Sub-module access (diagnostics / smoke tests) ────────────────────────
    const EarlyReflections& earlyReflections() const noexcept { return er_; }
    const TVFDNEngine&       fdn()             const noexcept { return fdn_; }

private:
    static int nextPowerOfTwo(int v) noexcept;

    EarlyReflections er_;
    TVFDNEngine      fdn_;

    // ── Global pre-delay ring (L and R independent) ──────────────────────────
    std::vector<float> preDelayRingL_;
    std::vector<float> preDelayRingR_;
    int preDelayMask_    = 0;
    int preDelayWriteL_  = 0;
    int preDelayWriteR_  = 0;
    int preDelaySamples_ = 0;

    // ── Scratch buffers (all sized in prepare) ───────────────────────────────
    std::vector<float> delayedL_;   // pre-delayed input, fed to ER + FDN
    std::vector<float> delayedR_;
    std::vector<float> erOutL_;
    std::vector<float> erOutR_;
    std::vector<float> fdnInL_;
    std::vector<float> fdnInR_;
    std::vector<float> fdnOutL_;
    std::vector<float> fdnOutR_;

    // ── Parameters ───────────────────────────────────────────────────────────
    float masterWet_    = 1.0f;
    float distance_     = 0.5f;
    float erGainCached_   = 0.7071f;  // cos(0.5 * π/2)
    float tailGainCached_ = 0.7071f;  // sin(0.5 * π/2)

    bool prepared_ = false;
};
