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
 * All parameters live in an AudioProcessorValueTreeState (APVTS). Raw
 * std::atomic<float>* pointers are cached in prepareToPlay() via
 * getRawParameterValue(). At the top of each processBlock() call these
 * atomics are loaded once (sequential-consistency) and forwarded to the
 * appropriate ReverbEngine setter.
 *
 * ReverbEngine's own one-pole smoothers absorb step changes from the APVTS,
 * so the audio thread never experiences a discontinuity regardless of
 * automation resolution.
 *
 * DecayEQ update throttling
 * ──────────────────────────
 * setDecayEQ() triggers RBJ coefficient recalculation (pow/sin/cos) for all
 * 16 FDN channels — expensive if called every block. Shadow values detect
 * parameter changes and skip the call when nothing has moved, limiting the
 * cost to changed-parameter events only (~block-rate updates from the UI).
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

    int  getNumPrograms()                                  override { return 1; }
    int  getCurrentProgram()                               override { return 0; }
    void setCurrentProgram(int)                            override {}
    const juce::String getProgramName(int)                 override { return {}; }
    void changeProgramName(int, const juce::String&)       override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── APVTS ────────────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter ID strings (shared with Editor)
    static constexpr const char* kParamPreDelay    = "preDelay";
    static constexpr const char* kParamDistance    = "distance";
    static constexpr const char* kParamMasterWet   = "masterWet";
    static constexpr const char* kParamFeedback    = "fdnFeedback";
    static constexpr const char* kParamModDepth    = "fdnModDepth";
    static constexpr const char* kParamLowFreq     = "decayLowFreq";
    static constexpr const char* kParamLowT60      = "decayLowT60";
    static constexpr const char* kParamMidFreq     = "decayMidFreq";
    static constexpr const char* kParamMidT60      = "decayMidT60";
    static constexpr const char* kParamHighFreq    = "decayHighFreq";
    static constexpr const char* kParamHighT60     = "decayHighT60";

private:
    ReverbEngine reverbEngine_;

    // Cached raw parameter pointers — set once in prepareToPlay, read in processBlock
    std::atomic<float>* pPreDelay_   = nullptr;
    std::atomic<float>* pDistance_   = nullptr;
    std::atomic<float>* pMasterWet_  = nullptr;
    std::atomic<float>* pFeedback_   = nullptr;
    std::atomic<float>* pModDepth_   = nullptr;
    std::atomic<float>* pLowFreq_    = nullptr;
    std::atomic<float>* pLowT60_     = nullptr;
    std::atomic<float>* pMidFreq_    = nullptr;
    std::atomic<float>* pMidT60_     = nullptr;
    std::atomic<float>* pHighFreq_   = nullptr;
    std::atomic<float>* pHighT60_    = nullptr;

    // Shadow values — only call setDecayEQ when at least one has changed
    float prevLowFreq_  = -1.0f, prevLowT60_  = -1.0f;
    float prevMidFreq_  = -1.0f, prevMidT60_  = -1.0f;
    float prevHighFreq_ = -1.0f, prevHighT60_ = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPluginProcessor)
};
