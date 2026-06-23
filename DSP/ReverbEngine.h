#pragma once

#include "EarlyReflections.h"
#include "FractionalDelayLine.h"
#include "TVFDNEngine.h"

#include <cstdint>
#include <vector>

/**
 * Top-level reverb engine facade (Phase 1 + Phase 2).
 *
 * Signal chain
 * ────────────
 *
 *   Dry L ──────────────────────────────────────────────────────────────────────────────────────┐
 *           │                                                                                    │
 *           └──► FractionalDelayLine ──► delayedL ──┬──► EarlyReflections ──► erOutL ──► erGain ──┐
 *                 (Thiran allpass)                  │                                              + ──► wetL
 *                 (per-sample smoothed delay)        └──► FDN input ──► TVFDNEngine ──► fdnOutL ──► tailGain ─┘
 *
 *   (same topology for R)
 *
 * Both ER and FDN receive the same fractionally-delayed signal, so the tail
 * and the reflections always depart from exactly the same moment in time.
 * The master dry signal is undelayed, preserving the original transient.
 *
 * Pre-delay automation (Doppler glide)
 * ─────────────────────────────────────
 * setPreDelayMs() writes to preDelayMsTarget_. A one-pole lowpass advances
 * preDelayMsCurrent_ toward the target one sample at a time. Because
 * FractionalDelayLine::readSample() accepts a float, a slowly-changing read
 * position produces a smooth, tape-style pitch glide — no clicks.
 *
 * Distance automation (zipper-free crossfade)
 * ────────────────────────────────────────────
 * setDistance() writes to distanceTarget_. A one-pole lowpass advances
 * distanceCurrent_ per sample inside processBlock. erGain and tailGain are
 * recomputed from distanceCurrent_ on every sample — no jumps possible.
 * Equal-power crossfade: erGain = cos(d * π/2), tailGain = sin(d * π/2).
 * At d=0.5 both channels are at 0.707 (–3 dB) — no dip in perceived loudness.
 *
 * Real-time constraints
 * ─────────────────────
 *   • All scratch buffers and FDLs sized in prepare(); never reallocated.
 *   • processBlock() is allocation-free and lock-free.
 *   • No malloc in the audio thread.
 *   • sin()/cos() called once per sample for distance equal-power crossfade only.
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

    // ── Parameter setters ────────────────────────────────────────────────────

    /**
     * Automate the global pre-delay (0–500 ms).
     * Sets the target; smoothed per-sample in processBlock → tape-style Doppler
     * glide when modulated, zero clicks on step changes.
     */
    void setPreDelayMs(float ms) noexcept
    {
        preDelayMsTarget_ = std::clamp(ms, 0.0f, kMaxPreDelayMs);
    }

    /**
     * Distance (0..1) — linear crossfade between ER and FDN tail.
     * Sets the target; smoothed per-sample in processBlock → zipper-free.
     *   0 = close (ER dominates), 0.5 = balanced, 1 = far (tail dominates).
     */
    void setDistance(float d) noexcept
    {
        distanceTarget_ = std::clamp(d, 0.0f, 1.0f);
    }
    float distance() const noexcept { return distanceTarget_; }

    /**
     * Master wet level (0 = dry only, 1 = wet only).
     * Sets the target; smoothed per-sample → zipper-free under automation.
     */
    void setMasterWet(float wet) noexcept { masterWetTarget_ = std::clamp(wet, 0.0f, 1.0f); }

    /** FDN feedback (0..0.99). */
    void setFdnFeedback(float g) noexcept { fdn_.setFeedback(g); }

    /** FDN LFO modulation depth (samples). */
    void setFdnModDepth(float d) noexcept { fdn_.setModDepth(d); }

    // ── Sub-module access (diagnostics / smoke tests) ────────────────────────
    const EarlyReflections& earlyReflections() const noexcept { return er_; }
    const TVFDNEngine&       fdn()             const noexcept { return fdn_; }

private:
    static constexpr float kMaxPreDelayMs = 500.0f;

    EarlyReflections    er_;
    TVFDNEngine         fdn_;

    // ── Fractional pre-delay lines (one per channel) ─────────────────────────
    // Sized for kMaxPreDelayMs in prepare(). Automatable with Doppler glide.
    FractionalDelayLine preDelayL_;
    FractionalDelayLine preDelayR_;

    // ── Pre-delay smoothing state ─────────────────────────────────────────────
    float preDelayMsTarget_  = 0.0f;
    float preDelayMsCurrent_ = 0.0f;
    float preDelaySmCoeff_   = 0.0f; // one-pole coefficient (~30 ms time constant)

    // ── Distance smoothing state ──────────────────────────────────────────────
    float distanceTarget_    = 0.5f;
    float distanceCurrent_   = 0.5f;
    float distanceSmoothCoeff_ = 0.0f; // one-pole coefficient (~50 ms time constant)

    // ── Scratch buffers (all sized in prepare) ───────────────────────────────
    std::vector<float> delayedL_;   // pre-delayed input → ER + FDN
    std::vector<float> delayedR_;
    std::vector<float> erOutL_;
    std::vector<float> erOutR_;
    std::vector<float> fdnInL_;
    std::vector<float> fdnInR_;
    std::vector<float> fdnOutL_;
    std::vector<float> fdnOutR_;

    // ── Engine state ─────────────────────────────────────────────────────────
    double sampleRate_      = 44100.0;
    int    maxBlockSize_    = 0;

    // Master wet — target/current pair for zipper-free DAW automation
    float  masterWetTarget_  = 1.0f;
    float  masterWetCurrent_ = 1.0f;
    float  masterWetSmCoeff_ = 0.0f; // one-pole coefficient (~50 ms time constant)

    bool   prepared_    = false;
};
