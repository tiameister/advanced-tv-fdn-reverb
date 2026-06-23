#include "ReverbEngine.h"

#include <algorithm>

void ReverbEngine::prepare(double        sampleRate,
                           int           maxBlockSize,
                           float         erLengthMs,
                           float         preDelayMs,
                           float         erDensityHz,
                           float         erMinSpacingMs,
                           std::uint32_t seedL,
                           std::uint32_t seedR)
{
    const int blockSize = std::max(1, maxBlockSize);

    // Pre-allocate all scratch buffers — never touched in processBlock
    erOutL_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    erOutR_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    fdnInL_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    fdnInR_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    fdnOutL_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    fdnOutR_.assign(static_cast<std::size_t>(blockSize), 0.0f);

    er_.prepare(sampleRate, blockSize,
                erLengthMs, preDelayMs,
                erDensityHz, erMinSpacingMs,
                seedL, seedR);

    // FDN runs 100% wet; ReverbEngine handles the master dry/wet blend.
    fdn_.prepare(sampleRate, blockSize);
    fdn_.setDryWet(1.0f);

    prepared_ = true;
    reset();
}

void ReverbEngine::reset() noexcept
{
    er_.reset();
    fdn_.reset();

    std::fill(erOutL_.begin(),  erOutL_.end(),  0.0f);
    std::fill(erOutR_.begin(),  erOutR_.end(),  0.0f);
    std::fill(fdnInL_.begin(),  fdnInL_.end(),  0.0f);
    std::fill(fdnInR_.begin(),  fdnInR_.end(),  0.0f);
    std::fill(fdnOutL_.begin(), fdnOutL_.end(), 0.0f);
    std::fill(fdnOutR_.begin(), fdnOutR_.end(), 0.0f);
}

void ReverbEngine::processBlock(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;

    // ── Step 1: Generate early reflections from the dry input ───────────────
    er_.processBlock(left, right,
                     erOutL_.data(), erOutR_.data(),
                     numSamples);

    // ── Step 2: Sum dry + ER → FDN input ────────────────────────────────────
    // The ER layer seeds the FDN tank with a spatially decorated version of
    // the source, ensuring the diffuse tail grows naturally from the ER burst.
    for (int i = 0; i < numSamples; ++i)
    {
        fdnInL_[i] = left[i]  + erOutL_[i];
        fdnInR_[i] = right[i] + erOutR_[i];
    }

    // ── Step 3: FDN processing (full wet) into dedicated output buffers ────────
    // erOutL/R must NOT be overwritten here — they carry the ER signal that
    // must survive into the final blend. FDN tail goes to fdnOutL/R.
    fdn_.processBlock(fdnInL_.data(), fdnInR_.data(),
                      fdnOutL_.data(), fdnOutR_.data(),
                      numSamples);

    // ── Step 4: Master dry/wet blend ─────────────────────────────────────────
    // wetL = ER burst + FDN diffuse tail — both are independently audible.
    const float wet = masterWet_;
    const float dry = 1.0f - wet;

    for (int i = 0; i < numSamples; ++i)
    {
        const float wetL = erOutL_[i] + fdnOutL_[i];
        const float wetR = erOutR_[i] + fdnOutR_[i];
        left[i]  = left[i]  * dry + wetL * wet;
        right[i] = right[i] * dry + wetR * wet;
    }
}
