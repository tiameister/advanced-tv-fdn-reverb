#include "DSP/ReverbEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool allFinite(const std::vector<float>& buf)
{
    for (float s : buf)
        if (!std::isfinite(s)) return false;
    return true;
}

/** One-pole LP filter applied in-place; returns final state. */
static float applyOnePoleLp(const std::vector<float>& in,
                            std::vector<float>&       out,
                            float                     coeff)
{
    float state = 0.0f;
    out.resize(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        state += coeff * (in[i] - state);
        out[i] = state;
    }
    return state;
}

/** Energy of samples[begin .. end-1]. */
static float energy(const std::vector<float>& buf, int begin, int end)
{
    float e = 0.0f;
    for (int i = begin; i < end; ++i)
        e += buf[static_cast<std::size_t>(i)] * buf[static_cast<std::size_t>(i)];
    return e;
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main()
{
    constexpr double sampleRate  = 48000.0;
    constexpr int    blockSize   = 512;
    constexpr float  kSrF        = static_cast<float>(sampleRate);

    // Total tail: 2.5 s (must be long enough to see LF still alive past 1 s)
    constexpr int totalSamples = static_cast<int>(2.5 * sampleRate);
    constexpr int numBlocks    = totalSamples / blockSize;

    // ── Construct & configure ─────────────────────────────────────────────────
    ReverbEngine engine;
    engine.prepare(sampleRate, blockSize,
                   /*erLengthMs*/     80.0f,
                   /*preDelayMs*/      0.0f,
                   /*erDensityHz*/  3000.0f,
                   /*erMinSpacingMs*/  1.0f,
                   /*seedL*/   0xABCD1234u,
                   /*seedR*/   0x5678EF90u);
    engine.setMasterWet(1.0f);
    engine.setDistance(1.0f);      // pure tail — no ER masking the decay shape
    engine.setFdnModDepth(0.0f);   // disable LFO so spectral content is stable

    // Unified Reverb Time API:
    //   reverbTime=2.5 s, bassDecayMult=2.0 → lowT60=5 s (slow bass)
    //                     hfDecayMult=0.08  → highT60=0.2 s (fast HF)
    // With feedback derived internally from reverbTime + avgDelay:
    //   actual_LF_T60 ≈ 2.5 s,  actual_HF_T60 ≈ 0.2 s
    engine.setReverbTime(2.5f);
    engine.setDecayShape(2.0f,   // bassDecayMult: bass lingers at 2× RT
                         1.0f,   // midDecayMult:  mids at RT
                         0.08f); // hfDecayMult:   HF at 0.08× RT = 0.2 s

    // ── Impulse test ──────────────────────────────────────────────────────────
    std::vector<float> left (static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.0f);
    left[0] = right[0] = 1.0f; // single impulse

    std::vector<float> tailL;
    tailL.reserve(static_cast<std::size_t>(totalSamples));

    for (int b = 0; b < numBlocks; ++b)
    {
        engine.processBlock(left.data(), right.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
            tailL.push_back(left[i]);

        std::fill(left.begin(),  left.end(),  0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    std::printf("Collected %d samples (%.2f s) of tail.\n",
                static_cast<int>(tailL.size()),
                static_cast<float>(tailL.size()) / kSrF);

    // ── CHECK 1: No NaN/Inf in entire tail ────────────────────────────────────
    if (!allFinite(tailL))
    {
        std::printf("FAIL: NaN or Inf detected in reverb output.\n");
        return 1;
    }
    std::printf("CHECK 1 PASS: output is finite throughout.\n");

    // ── CHECK 2: HF decays faster than LF ────────────────────────────────────
    // Split the tail into LF and HF bands using a one-pole LP at ~700 Hz.
    //   coeff = 1 - exp(-2π × 700 / 48000) ≈ 0.0855
    const float lpCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * 700.0f / kSrF);

    std::vector<float> lfBand; // LP output
    applyOnePoleLp(tailL, lfBand, lpCoeff);

    std::vector<float> hfBand(tailL.size());
    for (std::size_t i = 0; i < tailL.size(); ++i)
        hfBand[i] = tailL[i] - lfBand[i];

    // Measurement window: 0.5 s → 1.5 s (steady decay; avoids early-reflection burst)
    const int winBegin = static_cast<int>(0.5f * kSrF);
    const int winEnd   = static_cast<int>(1.5f * kSrF);

    const float lfEnergy  = energy(lfBand, winBegin, winEnd);
    const float hfEnergy  = energy(hfBand, winBegin, winEnd);
    const float totalWin  = lfEnergy + hfEnergy;

    std::printf("Energy in 0.5–1.5 s window:\n");
    std::printf("  LF (<700 Hz): %.4e\n", lfEnergy);
    std::printf("  HF (>700 Hz): %.4e\n", hfEnergy);

    if (totalWin <= 0.0f)
    {
        std::printf("FAIL: zero energy in measurement window — tail decayed too fast.\n"
                    "      Increase feedback_ or widen the window.\n");
        return 1;
    }

    // With actual_HF_T60 ≈ 0.25 s and actual_LF_T60 ≈ 2.5 s, at t=1 s:
    //   LF remaining:  10^(-3×1/2.5)  ≈ 5.6 × 10^(-2)   (still meaningful)
    //   HF remaining:  10^(-3×1/0.25) ≈ 1.0 × 10^(-12)  (essentially zero)
    // So lfEnergy should dominate heavily.
    if (hfEnergy >= lfEnergy)
    {
        std::printf("FAIL: HF energy (%.4e) ≥ LF energy (%.4e) — HF did not decay faster.\n",
                    hfEnergy, lfEnergy);
        return 1;
    }
    std::printf("CHECK 2 PASS: LF/HF energy ratio = %.1f× (HF decays faster).\n",
                lfEnergy / std::max(hfEnergy, 1e-30f));

    // ── CHECK 3: Tail eventually reaches near-silence (no runaway feedback) ──
    const int silenceBegin = static_cast<int>(2.0f * kSrF);
    const int silenceEnd   = static_cast<int>(tailL.size());
    const float lateEnergy = energy(tailL, silenceBegin, silenceEnd);

    const float earlyEnergy = energy(tailL, 0, static_cast<int>(0.1f * kSrF));
    if (earlyEnergy > 0.0f && lateEnergy > earlyEnergy)
    {
        std::printf("FAIL: late-tail energy (%.4e) > early energy (%.4e) — feedback instability!\n",
                    lateEnergy, earlyEnergy);
        return 1;
    }
    std::printf("CHECK 3 PASS: tail decays (late/early energy ratio = %.2e).\n",
                lateEnergy / std::max(earlyEnergy, 1e-30f));

    std::printf("\nPASS: decay_smoke_test — all checks passed.\n");
    return 0;
}
