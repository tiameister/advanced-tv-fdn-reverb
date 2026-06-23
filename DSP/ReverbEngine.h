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
 *   Dry L ──┬──► EarlyReflections ──► erOutL ──┬──► fdnInL ──► TVFDNEngine ──► fdnOutL ──┐
 *           │                                   │                                          │
 *           └───────────────────────────────► + ─┘         erOutL ──────────────────────► + ──► wetL
 *                                                                                                 │
 *                                                           dryL ─────────────────────────────► + ──► outL
 *   (same topology for R)
 *
 * In detail, for each sample:
 *   fdnInL  = dryL + erOutL           (ER seeds the FDN tank with spatial decoration)
 *   fdnOutL = FDN(fdnInL)             (diffuse tail only — FDN runs 100% wet)
 *   wetL    = erOutL + fdnOutL        (ER burst + FDN tail = complete wet signal)
 *   outL    = dryL * (1-masterWet) + wetL * masterWet
 *
 * The FDN output goes into fdnOutL/R (separate from erOutL/R) so the ER
 * contribution is preserved and heard independently in the final wet sum.
 *
 * Real-time constraints
 * ─────────────────────
 *   • All scratch buffers sized in prepare(); never reallocated in processBlock.
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
     * @param erLengthMs      ER window (ms). Default: 80 ms.
     * @param preDelayMs      Pre-delay before first tap (ms). Default: 0 ms.
     * @param erDensityHz     ER tap density (impulses/sec). Default: 3000.
     * @param erMinSpacingMs  Minimum tap spacing (ms). Default: 1 ms.
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

    /**
     * In-place stereo processing.  left/right are read AND written.
     */
    void processBlock(float* left, float* right, int numSamples) noexcept;

    // ── Parameter setters (call from UI/parameter thread, not audio thread) ──

    /** Master wet level (0 = dry only, 1 = wet only). */
    void setMasterWet(float wet)   noexcept { masterWet_ = std::clamp(wet, 0.0f, 1.0f); }

    /** ER contribution into the FDN input (0 = FDN fed from dry only). */
    void setErMix(float mix)       noexcept { er_.setErMix(std::clamp(mix, 0.0f, 1.0f)); }

    /** FDN feedback (0..0.99). */
    void setFdnFeedback(float g)   noexcept { fdn_.setFeedback(g); }

    /** FDN LFO modulation depth (samples). */
    void setFdnModDepth(float d)   noexcept { fdn_.setModDepth(d); }

    // ── Sub-module access (read-only; for diagnostics) ──────────────────────
    const EarlyReflections& earlyReflections() const noexcept { return er_; }
    const TVFDNEngine&       fdn()             const noexcept { return fdn_; }

private:
    EarlyReflections er_;
    TVFDNEngine      fdn_;

    // Scratch buffers — sized in prepare(), never reallocated in processBlock
    std::vector<float> erOutL_;   // ER output (preserved through FDN step)
    std::vector<float> erOutR_;
    std::vector<float> fdnInL_;   // dry + ER fed into the FDN
    std::vector<float> fdnInR_;
    std::vector<float> fdnOutL_;  // FDN tail output (separate from ER)
    std::vector<float> fdnOutR_;

    float masterWet_ = 1.0f;
    bool  prepared_  = false;
};
