#pragma once

#include <JuceHeader.h>
#include "DSP/ReverbEngine.h"

/**
 * ReverbPluginProcessor
 * ──────────────────────
 * Wraps the JUCE-free ReverbEngine in a JUCE AudioProcessor shell.
 *
 * Parameter strategy (no allocation on audio thread)
 * ───────────────────────────────────────────────────
 * All 12 parameters live in an AudioProcessorValueTreeState (APVTS). Raw
 * std::atomic<float>* pointers are cached in prepareToPlay() via
 * getRawParameterValue(). At the top of each processBlock() call these
 * atomics are loaded once (memory_order_relaxed — sufficient for float
 * parameters; no ordering needed between independent audio parameters) and
 * forwarded to the appropriate ReverbEngine setter.
 *
 * ReverbEngine's own one-pole smoothers absorb step changes so the audio
 * thread never experiences a discontinuity regardless of automation resolution.
 *
 * No Decay-EQ throttling required
 * ─────────────────────────────────
 * With the unified API, setReverbTime() and setDecayShape() are called every
 * block when their parameters move. These functions are O(NumChannels) floating-
 * point arithmetic — no pow/sin/cos per call. updateFilterCoefficients() (which
 * does call pow) is triggered only when the parameters actually change value.
 * The engine handles this internally via feedbackTarget_ + T60 targets vs the
 * one-pole smoothers, so no block-level epsilon gating is needed here.
 *
 * CPU budget (steady-state, no EQ change):
 *   16 channels × 3 biquads × (5 coeff MACs + 3 TDF-II MACs) ≈ 128 MAC/sample
 *   16 Hermite reads + FWHT + DC-blocker — fine at 48 kHz/512
 */
class ReverbPluginProcessor : public juce::AudioProcessor
{
public:
    ReverbPluginProcessor();
    ~ReverbPluginProcessor() override = default;

    // ── AudioProcessor overrides ─────────────────────────────────────────────
    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pro Reverb"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int  getNumPrograms()                            override { return 1; }
    int  getCurrentProgram()                         override { return 0; }
    void setCurrentProgram(int)                      override {}
    const juce::String getProgramName(int)           override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── APVTS ────────────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter ID strings (shared with Editor)
    // ── Main ──────────────────────────────────────────────────────────────
    static constexpr const char* kParamReverbTime  = "reverbTime";
    static constexpr const char* kParamSize        = "size";
    static constexpr const char* kParamPreDelay    = "preDelay";
    static constexpr const char* kParamDistance    = "distance";
    static constexpr const char* kParamMasterWet   = "masterWet";
    // ── Character ──────────────────────────────────────────────────────────
    static constexpr const char* kParamModDepth    = "fdnModDepth";
    static constexpr const char* kParamStereoWidth = "fdnStereoWidth";
    static constexpr const char* kParamErLength    = "erLength";
    static constexpr const char* kParamErDensity   = "erDensity";
    // ── Decay shape ────────────────────────────────────────────────────────
    static constexpr const char* kParamBassDecay   = "bassDecay";
    static constexpr const char* kParamMidDecay    = "midDecay";
    static constexpr const char* kParamHfDecay     = "hfDecay";

private:
    ReverbEngine reverbEngine_;

    // Pre-allocated mono scratch buffer — avoids allocation in processBlock mono fallback.
    std::vector<float> monoScratchR_;

    // Cached atomic pointers — set once in prepareToPlay, read in processBlock
    std::atomic<float>* pReverbTime_  = nullptr;
    std::atomic<float>* pSize_        = nullptr;
    std::atomic<float>* pPreDelay_    = nullptr;
    std::atomic<float>* pDistance_    = nullptr;
    std::atomic<float>* pMasterWet_   = nullptr;
    std::atomic<float>* pModDepth_    = nullptr;
    std::atomic<float>* pStereoWidth_ = nullptr;
    std::atomic<float>* pErLength_    = nullptr;
    std::atomic<float>* pErDensity_   = nullptr;
    std::atomic<float>* pBassDecay_   = nullptr;
    std::atomic<float>* pMidDecay_    = nullptr;
    std::atomic<float>* pHfDecay_     = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPluginProcessor)
};
